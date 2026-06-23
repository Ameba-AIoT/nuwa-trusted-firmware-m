/*
 * Copyright (c) 2025, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "tfm_hal_device_header.h"
#include "target_cfg.h"
#include "tfm_hal_platform.h"
#include "tfm_plat_defs.h"
#include "uart_stdout.h"
#include "flash_layout.h"
#include "fih.h"

/* LOGUART function from HAL */
extern void LOGUART_PutChar(uint8_t c);

extern const struct memory_region_limits memory_regions;

/* Initialize stdio for UART output */
void stdio_init(void)
{
	/* LOGUART is initialized by HAL, no additional setup needed */
}

void stdio_uninit(void)
{
}

FIH_RET_TYPE(enum tfm_hal_status_t) tfm_hal_platform_init(void)
{
	enum tfm_plat_err_t plat_err = TFM_PLAT_ERR_SYSTEM_ERR;
	fih_int fih_rc = FIH_FAILURE;

	plat_err = enable_fault_handlers();
	if (plat_err != TFM_PLAT_ERR_SUCCESS) {
		FIH_RET(fih_int_encode(TFM_HAL_ERROR_GENERIC));
	}

	plat_err = system_reset_cfg();
	if (plat_err != TFM_PLAT_ERR_SUCCESS) {
		FIH_RET(fih_int_encode(TFM_HAL_ERROR_GENERIC));
	}

	FIH_CALL(init_debug, fih_rc);
	if (fih_not_eq(fih_rc, fih_int_encode(TFM_PLAT_ERR_SUCCESS))) {
		FIH_RET(fih_int_encode(TFM_HAL_ERROR_GENERIC));
	}

	__enable_irq();
	stdio_init();

	plat_err = nvic_interrupt_target_state_cfg();
	if (plat_err != TFM_PLAT_ERR_SUCCESS) {
		FIH_RET(fih_int_encode(TFM_HAL_ERROR_GENERIC));
	}

	plat_err = nvic_interrupt_enable();
	if (plat_err != TFM_PLAT_ERR_SUCCESS) {
		FIH_RET(fih_int_encode(TFM_HAL_ERROR_GENERIC));
	}

#if defined(TEST_S_FPU) || defined(TEST_NS_FPU)
	/* Set IRQn in secure mode */
	NVIC_ClearTargetState(TFM_FPU_S_TEST_IRQ);

	/* Enable FPU secure test interrupt */
	NVIC_EnableIRQ(TFM_FPU_S_TEST_IRQ);
#endif

#if defined(TEST_NS_FPU)
	/* Set IRQn in non-secure mode */
	NVIC_SetTargetState(TFM_FPU_NS_TEST_IRQ);
#if (TFM_ISOLATION_LEVEL >= 2)
	/* On isolation level 2, FPU test ARoT service runs in unprivileged mode.
	 * Set SCB.CCR.USERSETMPEND as 1 to enable FPU test service to access STIR
	 * register.
	 */
	SCB->CCR |= SCB_CCR_USERSETMPEND_Msk;
#endif
#endif

	FIH_RET(fih_int_encode(TFM_HAL_SUCCESS));
}

#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
uint32_t tfm_hal_get_ns_VTOR(void)
{
#if defined(CONFIG_AMEBADPLUS) && defined(BL2)
	/* mcuboot BL2 does not run amebadplus stock BOOT_NsStart, so
	 * SCB_NS->VTOR is not yet set. Return the NS image vector table
	 * base (RSIP-mapped just after the mcuboot 0x400 header). */
	return (uint32_t)(NS_AP_LOGIC_BASE + 0x400);
#else
	return SCB_NS->VTOR; // Already Done in Bootloader
#endif
}

uint32_t tfm_hal_get_ns_MSP(void)
{
	// return *((uint32_t *)memory_regions.non_secure_code_start);
#ifdef CONFIG_AMEBADPLUS
#ifdef BL2
	return *((uint32_t *)(NS_AP_LOGIC_BASE + 0x400));
#else
	return __TZ_get_MSP_NS();
#endif
#else
#ifdef BL2
	return *((uint32_t *)(NS_AP_LOGIC_BASE + BL2_HEADER_SIZE));
#else
	return __TZ_get_MSP_NS(); // Already Done in Bootloader
#endif
#endif
}
#endif

uint32_t tfm_hal_get_ns_entry_point(void)
{
	// return *((uint32_t *)(memory_regions.non_secure_code_start + 4));
#ifdef CONFIG_AMEBADPLUS
#ifdef BL2
	/*
	 * mcuboot/BL2 path: NS image is RSIP-mapped at NS_AP_LOGIC_BASE.
	 * RSIP MMU_ID2 skips both the mcuboot header and the KM0 sub-image,
	 * so 0x0E000400 is the first instruction byte of the NS image
	 * vector table. Reset_Handler is vector_table[1].
	 *
	 * Note: the NS app must be linked with the absolute slot0_partition
	 * address (CONFIG_USE_DT_CODE_PARTITION=y), otherwise the vector
	 * table holds image-relative offsets and the jump goes to ROM.
	 */
	return *((uint32_t *)(NS_AP_LOGIC_BASE + 0x400 + 4));
#else
	PRAM_START_FUNCTION Image2EntryFun = (PRAM_START_FUNCTION)__image2_entry_func_entry__;
	return (uint32_t)Image2EntryFun->RamStartFun;
#endif
#else
#ifdef BL2
	return *((uint32_t *)(NS_AP_LOGIC_BASE + BL2_HEADER_SIZE + 4));
#else
	PRAM_START_FUNCTION Image2EntryFun = (PRAM_START_FUNCTION)__image2_entry_func__;
	return (uint32_t)Image2EntryFun->RamStartFun;
#endif
#endif
}

int stdio_output_string(const char *str, uint32_t len)
{
	int nChars = 0;

	for (/*Empty */; len > 0; --len) {
		LOGUART_PutChar(*str++);
		++nChars;
	}
	return nChars;
}

/* Redirects printf to STDIO_DRIVER in case of GNUARM */
int _write(int fd, char *str, int len)
{
	(void)fd;

	/* Send string and return the number of characters written */
	return stdio_output_string((const char *)str, (uint32_t)len);
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
	(void)data;
	RandBytes_Get(output, len);
	*olen = len;
	return 0;
}

int mbedtls_psa_external_get_random(void *context, uint8_t *output, size_t output_size, size_t *output_length)
{
	(void)context;
	RandBytes_Get(output, output_size);
	*output_length = output_size;
	return 0;
}
