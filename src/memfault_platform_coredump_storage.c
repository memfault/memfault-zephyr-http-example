//! @file
//!
//! Memfault coredump storage implementation for the RT1024 (Zephyr flash_map
//! API)

#include <zephyr/storage/flash_map.h>

#include "memfault/components.h"
#include "memfault/config.h"

// This is inspired by the Zephyr coredump_backend_flash_partition.c
// implementation
#define FLASH_PARTITION storage_partition
#define FLASH_PARTITION_ID FIXED_PARTITION_ID(FLASH_PARTITION)

#if !FIXED_PARTITION_EXISTS(FLASH_PARTITION)
  #error "Need a fixed partition named 'storage_partition'!"
#endif

#define FLASH_CONTROLLER DT_PARENT(DT_PARENT(DT_NODELABEL(FLASH_PARTITION)))

#define FLASH_WRITE_SIZE DT_PROP(FLASH_CONTROLLER, write_block_size)
#define FLASH_ERASE_SIZE DT_PROP(FLASH_CONTROLLER, erase_block_size)
// reg index 1 is size
#define FLASH_STORAGE_SIZE DT_PROP_BY_IDX(DT_NODELABEL(FLASH_PARTITION), reg, 1)

// Compute the memory-mapped address of the coredump storage partition, used for
// reading data.
// reg index 0 is offset
#define FLASH_STORAGE_PARTITION_OFFSET DT_PROP_BY_IDX(DT_NODELABEL(FLASH_PARTITION), reg, 0)
// reg index 2 is offset
#define FLASH_STORAGE_MEMMAPPED_ADDRESS \
  (DT_PROP_BY_IDX(DT_PARENT(FLASH_CONTROLLER), reg, 2) + FLASH_STORAGE_PARTITION_OFFSET)

#define MEMFAULT_COREDUMP_STORAGE_WRITE_SIZE FLASH_WRITE_SIZE
#include <string.h>

#include "memfault/ports/buffered_coredump_storage.h"

// Error writing to flash - should never happen & likely detects a configuration
// error Call the reboot handler which will halt the device if a debugger is
// attached and then reboot
MEMFAULT_NO_OPT static void prv_coredump_writer_assert_and_reboot(int error_code) {
  (void)error_code;
  memfault_platform_halt_if_debugging();
  memfault_platform_reboot();
}

static bool prv_op_within_flash_bounds(uint32_t offset, size_t data_len) {
  sMfltCoredumpStorageInfo info = { 0 };
  memfault_platform_coredump_storage_get_info(&info);
  return (offset + data_len) <= info.size;
}

void memfault_platform_coredump_storage_clear(void) {
  const struct flash_area *flash_area;
  int ret = flash_area_open(FLASH_PARTITION_ID, &flash_area);
  if (ret == 0) {
    ret = flash_area_erase(flash_area, 0, FLASH_ERASE_SIZE);
  }
  flash_area_close(flash_area);

  if (ret) {
    MEMFAULT_LOG_ERROR("Failed to clear coredump storage, rv=%d", ret);
  }
}

const sMfltCoredumpStorageInfo s_coredump_storage_info = {
  .size = FLASH_STORAGE_SIZE,
  .sector_size = FLASH_ERASE_SIZE,
};
void memfault_platform_coredump_storage_get_info(sMfltCoredumpStorageInfo *info) {
  *info = s_coredump_storage_info;
}

bool memfault_platform_coredump_storage_buffered_write(sCoredumpWorkingBuffer *blk) {
  const struct flash_area *flash_area;
  int ret = flash_area_open(FLASH_PARTITION_ID, &flash_area);
  if (ret == 0) {
    ret = flash_area_write(flash_area, blk->write_offset, blk->data, sizeof(blk->data));
  }
  flash_area_close(flash_area);

  if (ret != 0) {
    prv_coredump_writer_assert_and_reboot(ret);
  }

  return true;
}
bool memfault_platform_coredump_storage_read(uint32_t offset, void *data, size_t read_len) {
  if (!prv_op_within_flash_bounds(offset, read_len)) {
    return false;
  }

  // The internal flash is memory mapped so we can just use memcpy!
  const uint32_t start_addr = FLASH_STORAGE_MEMMAPPED_ADDRESS;
  memcpy(data, (void *)(start_addr + offset), read_len);
  return true;
}

bool memfault_platform_coredump_storage_erase(uint32_t offset, size_t erase_size) {
  if (!prv_op_within_flash_bounds(offset, erase_size)) {
    return false;
  }

  if (((offset % FLASH_ERASE_SIZE) != 0) || (erase_size % FLASH_ERASE_SIZE) != 0) {
    // configuration error
    prv_coredump_writer_assert_and_reboot(offset);
  }

  // erase block-by-block
  for (size_t sector_offset = 0; sector_offset < erase_size; sector_offset += FLASH_ERASE_SIZE) {
    const struct flash_area *flash_area;
    int ret = flash_area_open(FLASH_PARTITION_ID, &flash_area);
    if (ret == 0) {
      ret = flash_area_erase(flash_area, offset + sector_offset, FLASH_ERASE_SIZE);
    }

    flash_area_close(flash_area);

    if (ret != 0) {
      prv_coredump_writer_assert_and_reboot(ret);
    }
  }

  return true;
}
