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

void FLASH_Write_Lock(void)
{
	/* disable irq */
	PrevIrqStatus = __get_PRIMASK();
	__disable_irq();
}

void FLASH_Write_Unlock(void)
{
	/* restore irq */
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
