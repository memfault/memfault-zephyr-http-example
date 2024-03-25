//! @file
//!
//! Copyright (c) Memfault, Inc.
//! See License.txt for details
//!
//! Example app main

// clang-format off
#include "memfault/ports/zephyr/include_compatibility.h"

#include MEMFAULT_ZEPHYR_INCLUDE(device.h)
#include MEMFAULT_ZEPHYR_INCLUDE(devicetree.h)
#include MEMFAULT_ZEPHYR_INCLUDE(drivers/gpio.h)
#include MEMFAULT_ZEPHYR_INCLUDE(kernel.h)
#include MEMFAULT_ZEPHYR_INCLUDE(logging/log.h)
#include MEMFAULT_ZEPHYR_INCLUDE(shell/shell.h)
#include MEMFAULT_ZEPHYR_INCLUDE(drivers/hwinfo.h)
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>

#include "cdr.h"
#include "memfault/components.h"
#include "memfault/ports/zephyr/core.h"
// clang-format on

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if CONFIG_ZEPHYR_MEMFAULT_EXAMPLE_MEMORY_METRICS
  //! Size of memory to allocate on main stack
  //! This requires a larger allocation due to the method used to measure stack usage
  #define STACK_ALLOCATION_SIZE (CONFIG_MAIN_STACK_SIZE >> 2)
  //! Size of memory to allocate on system heap
  #define HEAP_ALLOCATION_SIZE (CONFIG_HEAP_MEM_POOL_SIZE >> 3)
  //! Value to sleep for observing metrics changes
  #define METRICS_OBSERVE_PERIOD MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS

//! Array of heap pointers
static void *heap_ptrs[4] = { NULL };

//! Keep a reference to the main thread for stack info
static struct k_thread *s_main_thread = NULL;
#endif  // CONFIG_ZEPHYR_MEMFAULT_EXAMPLE_MEMORY_METRICS

#if defined(CONFIG_MEMFAULT_HTTP_ENABLE)
MEMFAULT_STATIC_ASSERT(sizeof(CONFIG_MEMFAULT_PROJECT_KEY) > 1,
                       "Please set CONFIG_MEMFAULT_PROJECT_KEY in prj.conf");
sMfltHttpClientConfig g_mflt_http_client_config = {
  .api_key = CONFIG_MEMFAULT_PROJECT_KEY,
};
#endif  // CONFIG_MEMFAULT_HTTP_ENABLE

// Blink code taken from the zephyr/samples/basic/blinky example.
static void blink_forever(void) {
#if CONFIG_QEMU_TARGET
  k_sleep(K_FOREVER);
#else
  /* 1000 msec = 1 sec */
  #define SLEEP_TIME_MS 1000

  /* The devicetree node identifier for the "led0" alias. */
  #define LED0_NODE DT_ALIAS(led0)

  static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

  int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
  if (ret < 0) {
    return;
  }

  while (1) {
    gpio_pin_toggle_dt(&led);
    k_msleep(SLEEP_TIME_MS);
  }
#endif  // CONFIG_QEMU_TARGET
}

static const char *prv_get_device_id(void) {
  uint8_t dev_id[16] = { 0 };
  static char dev_id_str[sizeof(dev_id) * 2 + 1];
  static const char *dev_str = "UNKNOWN";

  // Obtain the device id
  ssize_t length = hwinfo_get_device_id(dev_id, sizeof(dev_id));

  // If this fails for some reason, use a fixed ID instead
  if (length <= 0) {
    dev_str = CONFIG_SOC_SERIES "-test";
    length = strlen(dev_str);
  } else {
    // Render the obtained serial number in hexadecimal representation
    for (size_t i = 0; i < length; i++) {
      (void)snprintf(&dev_id_str[i * 2], sizeof(dev_id_str), "%02x", dev_id[i]);
    }
    dev_str = dev_id_str;
  }

  return dev_str;
}

void memfault_platform_get_device_info(sMemfaultDeviceInfo *info) {
  *info = (sMemfaultDeviceInfo){
    .device_serial = prv_get_device_id(),
    .software_type = "zephyr-app",
    .software_version =
      CONFIG_ZEPHYR_MEMFAULT_EXAMPLE_SOFTWARE_VERSION "+" ZEPHYR_MEMFAULT_EXAMPLE_GIT_SHA1,
    .hardware_version = CONFIG_BOARD,
  };
}

#if CONFIG_ZEPHYR_MEMFAULT_EXAMPLE_THREAD_TOGGLE
K_THREAD_STACK_DEFINE(test_thread_stack_area, 1024);
static struct k_thread test_thread;

static void prv_test_thread_function(void *arg0, void *arg1, void *arg2) {
  ARG_UNUSED(arg0);
  ARG_UNUSED(arg1);
  ARG_UNUSED(arg2);

  k_sleep(K_FOREVER);
}

static void prv_test_thread_work_handler(struct k_work *work) {
  ARG_UNUSED(work);

  static bool started = false;
  if (started) {
    LOG_INF("ending test_thread ❌");
    k_thread_abort(&test_thread);
    started = false;
  } else {
    LOG_INF("starting test_thread ✅");
    k_thread_create(&test_thread, test_thread_stack_area,
                    K_THREAD_STACK_SIZEOF(test_thread_stack_area), prv_test_thread_function, NULL,
                    NULL, NULL, 7, 0, K_FOREVER);
    k_thread_name_set(&test_thread, "test_thread");
    k_thread_start(&test_thread);
    started = true;
  }
}
K_WORK_DEFINE(s_test_thread_work, prv_test_thread_work_handler);

//! Timer handlers run from an ISR so we dispatch the heartbeat job to the
//! worker task
static void prv_test_thread_timer_expiry_handler(struct k_timer *dummy) {
  k_work_submit(&s_test_thread_work);
}
K_TIMER_DEFINE(s_test_thread_timer, prv_test_thread_timer_expiry_handler, NULL);

static void prv_init_test_thread_timer(void) {
  k_timer_start(&s_test_thread_timer, K_SECONDS(10), K_SECONDS(10));

  // fire one time right away
  k_work_submit(&s_test_thread_work);
}
#else
static void prv_init_test_thread_timer(void) { }
#endif  // CONFIG_ZEPHYR_MEMFAULT_EXAMPLE_THREAD_TOGGLE

#if CONFIG_ZEPHYR_MEMFAULT_EXAMPLE_MEMORY_METRICS

//! Helper function to collect metric value on main thread stack usage.
static void prv_collect_main_thread_stack_free(void) {
  if (s_main_thread == NULL) {
    return;
  }

  size_t unused = 0;
  int rc = k_thread_stack_space_get(s_main_thread, &unused);
  if (rc == 0) {
    rc = MEMFAULT_METRIC_SET_UNSIGNED(MainStack_MinBytesFree, unused);
    if (rc) {
      LOG_ERR("Error[%d] setting MainStack_MinBytesFree", rc);
    }
  } else {
    LOG_ERR("Error getting thread stack usage[%d]", rc);
  }
}

//! Shell function to exercise example memory metrics
//!
//! This function demonstrates the change in stack and heap memory metrics
//! as memory is allocated and deallocated from these regions.
//!
//! @warning This code uses `memfault_metrics_heartbeat_debug_trigger` which is not intended
//! to be used in production code. This functions use here is solely to demonstrate the metrics
//! values changing. Production applications should rely on the heartbeat timer to trigger
//! collection
static int prv_run_example_memory_metrics(const struct shell *shell, size_t argc, char **argv) {
  ARG_UNUSED(shell);
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  if (s_main_thread == NULL) {
    return 0;
  }

  // Next two loops demonstrate heap usage metric
  for (size_t i = 0; i < ARRAY_SIZE(heap_ptrs); i++) {
    heap_ptrs[i] = k_malloc(HEAP_ALLOCATION_SIZE);
  }

  // Collect data after allocation
  memfault_metrics_heartbeat_debug_trigger();

  for (size_t i = 0; i < ARRAY_SIZE(heap_ptrs); i++) {
    k_free(heap_ptrs[i]);
    heap_ptrs[i] = NULL;
  }

  // Collect data after deallocation
  memfault_metrics_heartbeat_debug_trigger();
  return 0;
}

SHELL_CMD_REGISTER(memory_metrics, NULL, "Collects runtime memory metrics from application",
                   prv_run_example_memory_metrics);

// Override function to collect the app metric MainStack_MinBytesFree
// and print current metric values
void memfault_metrics_heartbeat_collect_data(void) {
  prv_collect_main_thread_stack_free();
}

//! Helper function to demonstrate changes in stack metrics
static void prv_run_stack_metrics_example(void) {
  volatile uint8_t stack_array[STACK_ALLOCATION_SIZE];
  memset((uint8_t *)stack_array, 0, STACK_ALLOCATION_SIZE);
}
#endif  // CONFIG_ZEPHYR_MEMFAULT_EXAMPLE_MEMORY_METRICS

#if defined(CONFIG_MEMFAULT_FAULT_HANDLER_RETURN)
  #include MEMFAULT_ZEPHYR_INCLUDE(fatal.h)
void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf) {
  printk("User fatal handler invoked with reason: %d. Rebooting!\n", reason);
  memfault_platform_reboot();
}
#endif

#include "memfault/ports/zephyr/http.h"
static void initialize_net(void) {
#if defined(CONFIG_NETWORKING)
  memfault_zephyr_port_install_root_certs();

  struct net_if *iface = net_if_get_default();

  if (!iface) {
    LOG_ERR("No network interface found");
    return;
  }

  if (net_if_flag_is_set(iface, NET_IF_DORMANT)) {
    LOG_INF("Waiting for interface to be up");
    while (!net_if_is_up(iface)) {
      k_sleep(K_MSEC(100));
    }
  }

  LOG_INF("Starting DHCP to obtain IP address");
  net_dhcpv4_start(iface);
  (void)net_mgmt_event_wait_on_iface(iface, NET_EVENT_IPV4_DHCP_BOUND, NULL, NULL, NULL, K_FOREVER);

  LOG_INF("Network is up");

  // print out the IP address
  char addr_str[NET_IPV4_ADDR_LEN];
  LOG_INF("IP Address: %s", net_addr_ntop(AF_INET,
  // the interface ip address structure was changed in zephyr commit
  // 1b0f9e865e35a6b3e1ca8aad7a67f7cfbfc2e666
  #if MEMFAULT_ZEPHYR_VERSION_GT(3, 6)
                                          &iface->config.ip.ipv4->unicast[0].ipv4.address.in_addr,
  #else
                                          &iface->config.ip.ipv4->unicast[0].address.in_addr,
  #endif
                                          addr_str, sizeof(addr_str)));

#endif  // CONFIG_NETWORKING
}

int main(void) {
  printk(MEMFAULT_BANNER_COLORIZED);
  LOG_INF("👋 Memfault Demo App! Board %s\n", CONFIG_BOARD);
  memfault_device_info_dump();

  memfault_cdr_register_source(&g_custom_data_recording_source);

#if !defined(CONFIG_MEMFAULT_RECORD_REBOOT_ON_SYSTEM_INIT)
  memfault_zephyr_collect_reset_info();
#endif

  initialize_net();

#if CONFIG_ZEPHYR_MEMFAULT_EXAMPLE_MEMORY_METRICS
  s_main_thread = k_current_get();

  // @warning This code uses `memfault_metrics_heartbeat_debug_trigger` which is not intended
  // to be used in production code. This functions use here is solely to demonstrate the metrics
  // values changing. Production applications should rely on the heartbeat timer to trigger
  // collection

  // Collect a round of metrics to show initial stack usage
  memfault_metrics_heartbeat_debug_trigger();

  prv_run_stack_metrics_example();

  // Collect another round to show change in stack metrics
  memfault_metrics_heartbeat_debug_trigger();
#endif

  prv_init_test_thread_timer();

  blink_forever();

  return 0;
}
