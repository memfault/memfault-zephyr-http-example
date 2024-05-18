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
#include "memfault/ports/zephyr/http.h"
// clang-format on

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// Blink code taken from the zephyr/samples/basic/blinky example.
static void blink_forever(void) {
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

static void initialize_net(void) {
#if defined(CONFIG_NETWORKING)
  #if defined(CONFIG_MEMFAULT_METRICS_CONNECTIVITY_CONNECTED_TIME)
  memfault_metrics_connectivity_connected_state_change(kMemfaultMetricsConnectivityState_Started);
  #endif

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

  #if defined(CONFIG_MEMFAULT_METRICS_CONNECTIVITY_CONNECTED_TIME)
  memfault_metrics_connectivity_connected_state_change(kMemfaultMetricsConnectivityState_Connected);
  #endif

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

// put the blink_forever in its own thread.
K_THREAD_DEFINE(blink_forever_thread, 1024, blink_forever, NULL, NULL, NULL, K_PRIO_PREEMPT(8), 0,
                0);

int main(void) {
  printk(MEMFAULT_BANNER_COLORIZED);
  LOG_INF("👋 Memfault Demo App! Board %s\n", CONFIG_BOARD);
  memfault_device_info_dump();

  memfault_cdr_register_source(&g_custom_data_recording_source);

#if !defined(CONFIG_MEMFAULT_RECORD_REBOOT_ON_SYSTEM_INIT)
  memfault_zephyr_collect_reset_info();
#endif

  initialize_net();

  return 0;
}
