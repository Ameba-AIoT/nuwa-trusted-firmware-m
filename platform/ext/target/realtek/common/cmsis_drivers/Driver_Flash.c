/*
 * Copyright (c) 2013-2022 ARM Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <stdint.h>
#include "Driver_Flash.h"
#include "RTE_Device.h"
#include "cmsis.h"

#ifndef ARG_UNUSED
#define ARG_UNUSED(arg)  ((void)arg)
#endif

/* Driver version */
#define ARM_FLASH_DRV_VERSION      ARM_DRIVER_VERSION_MAJOR_MINOR(1, 1)
#define ARM_FLASH_DRV_ERASE_VALUE  0xFF

/* Flash memory emulated over external SSRAM memory */
#define FLASH0_SIZE                    0x00400000  /* 4 MB */
#define FLASH0_SECTOR_SIZE             0x00001000  /* 4 kB */
#define FLASH0_PAGE_SIZE               0x00000100  /* 256 B */
#define FLASH0_PROGRAM_UNIT            0x1         /* Minimum write size */
#define FLASH_CHECK_BUF_SIZE           64          /* size checked per read */

/**
 * Data width values for ARM_FLASH_CAPABILITIES::data_width
 * \ref ARM_FLASH_CAPABILITIES
 */
 enum {
    DATA_WIDTH_8BIT   = 0u,
    DATA_WIDTH_16BIT,
    DATA_WIDTH_32BIT,
    DATA_WIDTH_ENUM_SIZE
};

static const uint32_t data_width_byte[DATA_WIDTH_ENUM_SIZE] = {
    sizeof(uint8_t),
    sizeof(uint16_t),
    sizeof(uint32_t),
};

/*
 * ARM FLASH device structure
 *
 * There is no real flash memory for code on MPS2 board. Instead a code SRAM is
 * used for code storage: ZBT SSRAM1. This driver just emulates a flash
 * interface and behaviour on top of the SRAM memory.
 */
struct arm_flash_dev_t {
    ARM_FLASH_INFO *data;         /*!< FLASH data */
};

/* Flash Status */
static ARM_FLASH_STATUS FlashStatus = {0, 0, 0};

/* Driver Version */
static const ARM_DRIVER_VERSION DriverVersion = {
    ARM_FLASH_API_VERSION,
    ARM_FLASH_DRV_VERSION
};

/* Driver Capabilities */
static const ARM_FLASH_CAPABILITIES DriverCapabilities = {
    0, /* event_ready */
    0, /* data_width = 0:8-bit, 1:16-bit, 2:32-bit */
    1  /* erase_chip */
};

static int32_t is_range_valid(struct arm_flash_dev_t *flash_dev,
                              uint32_t offset)
{
    uint32_t flash_limit = 0;
    int32_t rc = 0;

    flash_limit = (flash_dev->data->sector_count * flash_dev->data->sector_size)
                   - 1;

    if (offset > flash_limit) {
        rc = -1;
    }
    return rc;
}

static int32_t is_write_aligned(struct arm_flash_dev_t *flash_dev,
                                uint32_t param)
{
    int32_t rc = 0;

    if ((param % flash_dev->data->program_unit) != 0) {
        rc = -1;
    }
    return rc;
}

static int32_t is_sector_aligned(struct arm_flash_dev_t *flash_dev,
                                 uint32_t offset)
{
    int32_t rc = 0;

    if ((offset % flash_dev->data->sector_size) != 0) {
        rc = -1;
    }
    return rc;
}

static int32_t is_flash_ready_to_write(uint32_t addr, uint32_t cnt)
{
    uint8_t read_buf[FLASH_CHECK_BUF_SIZE];
    uint32_t offset = 0;
    uint32_t check_len;
    uint32_t i;

    while (cnt > 0) {
        check_len = (cnt > FLASH_CHECK_BUF_SIZE) ? FLASH_CHECK_BUF_SIZE : cnt;

        if (FLASH_ReadStream(addr + offset, check_len, read_buf) != 1) {
            return -1;
        }

        for (i = 0; i < check_len; i++) {
            if(read_buf[i] != ARM_FLASH_DRV_ERASE_VALUE) {
                return -1;
            }
        }

        cnt -= check_len;
        offset += check_len;
    }

    return 0;
}

#if (RTE_FLASH0)
static uint32_t PrevIrqStatus;

/*
 * Cross-core IPC flash lock/unlock for RTL8721F (AmebaG2) TF-M S side.
 *
 * KM4TZ and KM4NS share one physical SPI Flash. When KM4TZ erases flash,
 * the SPIC enters BUSY state and physically stalls all KM4NS XIP fetches.
 * KM4NS FreeRTOS cannot run, its IWDG refresh timer (500 ms) cannot fire,
 * and the AON IWDG (~4 s) times out causing a global system reset.
 *
 * This matches SDK ameba_flash_ram.c (CONFIG_ARM_CORE_CM4_KM4TZ block):
 *   LOCK:   IPC_SEMTake → notify KM4NS (LOCK) → wait ACK → disable IRQ
 *   UNLOCK: enable IRQ → notify KM4NS (UNLOCK) → wait ACK → IPC_SEMFree
 *
 * On receiving LOCK, KM4NS records a hardware-timer timestamp and ACKs
 * immediately (before the erase starts).  On receiving UNLOCK, KM4NS
 * computes the stall duration from the hardware timer and calls
 * xTaskIncrementTick() to compensate the frozen FreeRTOS tick count,
 * which causes the overdue IWDG timer to fire and feeds the watchdog.
 *
 * Hardware constants (AmebaG2 / RTL8721F):
 *   IPC shared memory : __ipc_memory_start__ = 0x20004400  (ROM symbol)
 *   IPC channel       : IPC_A2N_FLASHPG_REQ  = 4  (AP->NP)
 *   msg slot index    : 16 * IPC_TX_CHANNEL_SWITCH(IPC_AP_TO_NP=1) + 4 = 20
 *   Message address   : 0x20004400 + 20*16 = 0x20004540
 *   IPCAP Secure base : IPC0_REG_BASE_S = 0x50815000
 *   TX trigger bit    : BIT(4 + IPC_TX_CHANNEL_SHIFT=16) = BIT(20) = 0x100000
 *
 * Requires the km4ns_image2_all.bin blob to be built with
 * CONFIG_WHC_INTF_IPC=y so that FLASH_Write_IPC_Int is present.
 */
#if defined(SOC_AMEBADPLUS)
/*
 * AmebaDplus (RTL872xDA): NP is KM0. Derive all IPC parameters from the HAL
 * symbols so this tracks the HAL layout automatically instead of hardcoding:
 *   - trigger reg  : the sender writes its OWN IPC device. On this SoC
 *                    IPC_GetDev(IPC_KM4_TO_KM0, TX) == IPCKM4_DEV_S.
 *   - msg slot     : same index the KM0 receiver reads via ipc_get_message():
 *                    IPC_TX_CHANNEL_NUM * IPC_TX_CHANNEL_SWITCH(dir) + channel.
 *   - trigger bit  : BIT(channel + IPC_TX_CHANNEL_SHIFT).
 * (The IPC shared-mem base __km0_ipc_memory_start__ is a linker symbol that is
 *  not resolved in the minimal TF-M secure image, so its numeric value is used;
 *  keep it in sync with the HAL rom-symbol ld if the layout ever moves.)
 */
#include "ameba_soc.h"
#define FLASH_IPC_KM0_MEM_BASE 0x20004e00UL /* == __km0_ipc_memory_start__ */
#define FLASH_IPC_MSG_ADDR  (FLASH_IPC_KM0_MEM_BASE + \
	(IPC_TX_CHANNEL_NUM * IPC_TX_CHANNEL_SWITCH(IPC_KM4_TO_KM0) + \
	 IPC_A2N_FLASHPG_REQ) * sizeof(IPC_MSG_STRUCT))
#define FLASH_IPC_REG_BASE  ((uintptr_t)IPCKM4_DEV_S)                 /* IPC_DATA @ offset 0 */
#define FLASH_IPC_TX_BIT    BIT(IPC_A2N_FLASHPG_REQ + IPC_TX_CHANNEL_SHIFT)
#else /* amebag2 (RTL8721F): NP is KM4NS -- see original derivation above */
#define FLASH_IPC_MSG_ADDR  0x20004540UL  /* IPC shared-memory slot */
#define FLASH_IPC_REG_BASE  0x50815000UL  /* IPCAP Secure register  */
#define FLASH_IPC_TX_BIT    0x00100000UL  /* BIT(20): channel-4 TX  */
#endif
#define FLASH_IPC_MSG_TYPE  1UL           /* IPC_USER_POINT         */
#define FLASH_IPC_CACHE_SZ  32U

#define WRITE_SYNC_CLEAR  0U
#define WRITE_SYNC_LOCK   1U
#define WRITE_SYNC_UNLOCK 2U

ALIGNMTO(FLASH_IPC_CACHE_SZ) static uint8_t Flash_Sync_Flag[FLASH_IPC_CACHE_SZ];

/*
 * Send one synchronous IPC notification to KM4NS and spin until ACK.
 * Must be called with IRQs still enabled (LOCK) or just re-enabled (UNLOCK).
 */
static void Flash_Write_Lock_IPC(uint8_t sync_type)
{
	volatile uint32_t *ipc_msg = (volatile uint32_t *)FLASH_IPC_MSG_ADDR;
	volatile uint32_t *ipc_reg = (volatile uint32_t *)FLASH_IPC_REG_BASE;

	Flash_Sync_Flag[0] = sync_type;
	DCache_Clean((uint32_t)Flash_Sync_Flag, sizeof(Flash_Sync_Flag));

	/* IPC_MSG_STRUCT: {msg_type, msg_ptr, msg_len, rsvd} */
	ipc_msg[0] = FLASH_IPC_MSG_TYPE;
	ipc_msg[1] = (uint32_t)Flash_Sync_Flag;
	ipc_msg[2] = 1U;
	ipc_msg[3] = 0U;
	DCache_Clean(FLASH_IPC_MSG_ADDR, 16U);

	*ipc_reg = FLASH_IPC_TX_BIT;  /* trigger IPC interrupt on KM4NS */

	do {
		DCache_Invalidate((uint32_t)Flash_Sync_Flag, sizeof(Flash_Sync_Flag));
	} while (Flash_Sync_Flag[0] != WRITE_SYNC_CLEAR);
}

/*
 * TF-M S / BL2 flash guard: interrupt-disable only (both amebadplus and amebag2).
 *
 * The IPC-based cross-core lock above requires the NP-side blob
 * (km0_image2_all.bin / km4ns_image2_all.bin) to be built with
 * CONFIG_WHC_INTF_IPC=y so that the FLASH_Write_IPC_Int handler is present.
 * Until updated blobs with this handler are available, use interrupt-disable
 * instead.  TF-M runs single-threaded in the Secure world and is the only
 * context that erases/writes flash at this stage, so this is safe.
 *
 * TODO: restore the IPC lock (move this block back inside #else) once the
 *       NP-side blobs include FLASH_Write_IPC_Int (CONFIG_WHC_INTF_IPC=y).
 */
void FLASH_Write_Lock(void)
{
	PrevIrqStatus = __get_PRIMASK();
	__disable_irq();
}

void FLASH_Write_Unlock(void)
{
	__set_PRIMASK(PrevIrqStatus);
}

static ARM_FLASH_INFO ARM_FLASH0_DEV_DATA = {
    .sector_info  = NULL,                  /* Uniform sector layout */
    .sector_count = FLASH0_SIZE / FLASH0_SECTOR_SIZE,
    .sector_size  = FLASH0_SECTOR_SIZE,
    .page_size    = FLASH0_PAGE_SIZE,
    .program_unit = FLASH0_PROGRAM_UNIT,
    .erased_value = ARM_FLASH_DRV_ERASE_VALUE};

static struct arm_flash_dev_t ARM_FLASH0_DEV = {
	.data        = &(ARM_FLASH0_DEV_DATA)
};

struct arm_flash_dev_t *FLASH0_DEV = &ARM_FLASH0_DEV;

/*
 * Functions
 */

static ARM_DRIVER_VERSION ARM_Flash_GetVersion(void)
{
    return DriverVersion;
}

static ARM_FLASH_CAPABILITIES ARM_Flash_GetCapabilities(void)
{
    return DriverCapabilities;
}

static int32_t ARM_Flash_Initialize(ARM_Flash_SignalEvent_t cb_event)
{
    ARG_UNUSED(cb_event);
    return ARM_DRIVER_OK;
}

static int32_t ARM_Flash_Uninitialize(void)
{
    /* Nothing to be done */
    return ARM_DRIVER_OK;
}

static int32_t ARM_Flash_PowerControl(ARM_POWER_STATE state)
{
    switch (state) {
    case ARM_POWER_FULL:
        /* Nothing to be done */
        return ARM_DRIVER_OK;
        break;

    case ARM_POWER_OFF:
    case ARM_POWER_LOW:
    default:
        return ARM_DRIVER_ERROR_UNSUPPORTED;
    }
}

static int32_t ARM_Flash_ReadData(uint32_t addr, void *data, uint32_t cnt)
{
	int32_t rc = 0;

	/* CMSIS ARM_FLASH_ReadData API requires the `addr` data type size aligned.
	 * Data type size is specified by the data_width in ARM_FLASH_CAPABILITIES.
	 */
	if (addr % data_width_byte[DriverCapabilities.data_width] != 0) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	/* Conversion between data items and bytes */
	cnt *= data_width_byte[DriverCapabilities.data_width];

	/* Check flash memory boundaries */
	rc = is_range_valid(FLASH0_DEV, addr + cnt);
	if (rc != 0) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	FLASH_ReadStream(addr, cnt, (u8 *)data);

	/* Conversion between bytes and data items */
	cnt /= data_width_byte[DriverCapabilities.data_width];

	return cnt;
}

static int32_t ARM_Flash_ProgramData(uint32_t addr, const void *data,
									 uint32_t cnt)
{
	int32_t rc = 0;

	/* Conversion between data items and bytes */
	cnt *= data_width_byte[DriverCapabilities.data_width];

	/* Check flash memory boundaries and alignment with minimal write size */
	rc  = is_range_valid(FLASH0_DEV, addr + cnt);
	rc |= is_write_aligned(FLASH0_DEV, addr);
	rc |= is_write_aligned(FLASH0_DEV, cnt);
	if (rc != 0) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

    /* Check if the flash area to write the data was erased previously */
    rc = is_flash_ready_to_write(addr, cnt);
    if (rc != 0) {
        return ARM_DRIVER_ERROR;
    }

	FLASH_WriteStream(addr, cnt, (u8 *)data);

	/* Conversion between bytes and data items */
	cnt /= data_width_byte[DriverCapabilities.data_width];

	return cnt;
}

static int32_t ARM_Flash_EraseSector(uint32_t addr)
{
	uint32_t rc = 0;

	rc  = is_range_valid(FLASH0_DEV, addr);
	rc |= is_sector_aligned(FLASH0_DEV, addr);
	if (rc != 0) {
		return ARM_DRIVER_ERROR_PARAMETER;
	}

	FLASH_EraseXIP(EraseSector, addr);

	return ARM_DRIVER_OK;
}

static int32_t ARM_Flash_EraseChip(void)
{
	FLASH_EraseXIP(EraseChip, 0);

	return ARM_DRIVER_OK;
}

static ARM_FLASH_STATUS ARM_Flash_GetStatus(void)
{
	return FlashStatus;
}

static ARM_FLASH_INFO *ARM_Flash_GetInfo(void)
{
	return FLASH0_DEV->data;
}

ARM_DRIVER_FLASH Driver_FLASH0 = {
    ARM_Flash_GetVersion,
    ARM_Flash_GetCapabilities,
    ARM_Flash_Initialize,
    ARM_Flash_Uninitialize,
    ARM_Flash_PowerControl,
    ARM_Flash_ReadData,
    ARM_Flash_ProgramData,
    ARM_Flash_EraseSector,
    ARM_Flash_EraseChip,
    ARM_Flash_GetStatus,
    ARM_Flash_GetInfo
};
#endif /* RTE_FLASH0 */
