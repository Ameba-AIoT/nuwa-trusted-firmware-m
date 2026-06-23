/*
 * Copyright (c) 2017-2024, Arm Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "tfm_hal_device_header.h"
#include "fih.h"
#include "target_cfg.h"
#include "region_defs.h"
#include "tfm_plat_defs.h"
#include "region.h"

extern uint32_t __vectors_start__[];

#ifdef PSA_API_TEST_IPC
#define PSA_FF_TEST_SECURE_UART2
#endif

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

struct platform_data_t tfm_peripheral_std_uart = {};

/* The section names come from the scatter file */
REGION_DECLARE(Load$$LR$$, LR_NS_PARTITION, $$Base);
REGION_DECLARE(Image$$, ER_VENEER, $$Base);
REGION_DECLARE(Image$$, VENEER_ALIGN, $$Limit);

#ifdef BL2
REGION_DECLARE(Load$$LR$$, LR_SECONDARY_PARTITION, $$Base);
#endif /* BL2 */

const struct memory_region_limits memory_regions = {
	.non_secure_code_start =
	(uint32_t) &REGION_NAME(Load$$LR$$, LR_NS_PARTITION, $$Base) +
	BL2_HEADER_SIZE,

	.non_secure_partition_base =
	(uint32_t) &REGION_NAME(Load$$LR$$, LR_NS_PARTITION, $$Base),

	.non_secure_partition_limit =
	(uint32_t) &REGION_NAME(Load$$LR$$, LR_NS_PARTITION, $$Base) +
	NS_PARTITION_SIZE - 1,

	.veneer_base = (uint32_t) &REGION_NAME(Image$$, ER_VENEER, $$Base),
	.veneer_limit = (uint32_t) &REGION_NAME(Image$$, VENEER_ALIGN, $$Limit),

#ifdef BL2
	.secondary_partition_base =
	(uint32_t) &REGION_NAME(Load$$LR$$, LR_SECONDARY_PARTITION, $$Base),

	.secondary_partition_limit =
	(uint32_t) &REGION_NAME(Load$$LR$$, LR_SECONDARY_PARTITION, $$Base) +
	SECONDARY_PARTITION_SIZE - 1,
#endif /* BL2 */
};

/* Allows software, via SAU, to define the code region as a NSC */
#define NSCCFG_CODENSC  1

/* Define Peripherals NS address range for the platform */
#define PERIPHERALS_BASE_NS_START (0x40000000)
#define PERIPHERALS_BASE_NS_END   (0x4FFFFFFF)

/* Enable system reset request for CPU 0 */
#define ENABLE_CPU0_SYSTEM_RESET_REQUEST (1U << 4U)

/* To write into AIRCR register, 0x5FA value must be write to the VECTKEY field,
 * otherwise the processor ignores the write.
 */
#define SCB_AIRCR_WRITE_MASK ((0x5FAUL << SCB_AIRCR_VECTKEY_Pos))

/* Debug configuration flags */
#define SPNIDEN_SEL_STATUS (0x01u << 7)
#define SPNIDEN_STATUS     (0x01u << 6)
#define SPIDEN_SEL_STATUS  (0x01u << 5)
#define SPIDEN_STATUS      (0x01u << 4)
#define NIDEN_SEL_STATUS   (0x01u << 3)
#define NIDEN_STATUS       (0x01u << 2)
#define DBGEN_SEL_STATUS   (0x01u << 1)
#define DBGEN_STATUS       (0x01u << 0)

#define All_SEL_STATUS (SPNIDEN_SEL_STATUS | SPIDEN_SEL_STATUS | \
                        NIDEN_SEL_STATUS | DBGEN_SEL_STATUS)

enum tfm_plat_err_t enable_fault_handlers(void)
{
	/* Explicitly set secure fault priority to the highest */
	NVIC_SetPriority(SecureFault_IRQn, 0);

	/* Enables BUS, MEM, USG and Secure faults */
	SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk
				  | SCB_SHCSR_BUSFAULTENA_Msk
				  | SCB_SHCSR_MEMFAULTENA_Msk
				  | SCB_SHCSR_SECUREFAULTENA_Msk;
	return TFM_PLAT_ERR_SUCCESS;
}

enum tfm_plat_err_t system_reset_cfg(void)
{
	// struct sysctrl_t *sysctrl = (struct sysctrl_t *)CMSDK_SYSCTRL_BASE_S;
	uint32_t reg_value = SCB->AIRCR;

	/* Enable system reset request for CPU 0, to be triggered via
	 * NVIC_SystemReset function.
	 */
	// sysctrl->resetmask |= ENABLE_CPU0_SYSTEM_RESET_REQUEST;

	/* Clear SCB_AIRCR_VECTKEY value */
	reg_value &= ~(uint32_t)(SCB_AIRCR_VECTKEY_Msk);

	/* Enable system reset request only to the secure world */
	reg_value |= (uint32_t)(SCB_AIRCR_WRITE_MASK | SCB_AIRCR_SYSRESETREQS_Msk);

	SCB->AIRCR = reg_value;

	return TFM_PLAT_ERR_SUCCESS;
}

FIH_RET_TYPE(enum tfm_plat_err_t) init_debug(void)
{
	FIH_RET(fih_int_encode(TFM_PLAT_ERR_SUCCESS));
}

/*----------------- NVIC interrupt target state to NS configuration ----------*/
enum tfm_plat_err_t nvic_interrupt_target_state_cfg(void)
{
	/* Target every interrupt to NS; unimplemented interrupts will be WI */
	for (uint8_t i = 0; i < sizeof(NVIC->ITNS) / sizeof(NVIC->ITNS[0]); i++) {
		NVIC->ITNS[i] = 0xFFFFFFFF;
	}

	return TFM_PLAT_ERR_SUCCESS;
}

/*----------------- NVIC interrupt enabling for S peripherals ----------------*/
enum tfm_plat_err_t nvic_interrupt_enable(void)
{
	return TFM_PLAT_ERR_SUCCESS;
}

/*------------------- SAU/IDAU configuration functions -----------------------*/
#if defined(PSA_API_TEST_NS) && !defined(PSA_API_TEST_IPC)
#define DEV_APIS_TEST_NVMEM_REGION_START (NS_DATA_LIMIT + 1)
#define DEV_APIS_TEST_NVMEM_REGION_LIMIT \
    (DEV_APIS_TEST_NVMEM_REGION_START + DEV_APIS_TEST_NVMEM_REGION_SIZE - 1)
#endif

struct sau_cfg_t {
	uint32_t RBAR;
	uint32_t RLAR;
	bool nsc;
};

const struct sau_cfg_t sau_cfg[] = {
	{
		0x0001E000,
		0x000447FF,
		false,
	},
	{
		SPI_FLASH_BASE,
		(u32)__km4_boot_text_start__ - IMAGE_HEADER_LEN - 1,
		false,
	},
	{
		0x10000000,
		__vectors_start__ - 0x20 - 1,
		false,
	},
	{
		(uint32_t) &REGION_NAME(Image$$, ER_VENEER, $$Base),
		(uint32_t) &REGION_NAME(Image$$, VENEER_ALIGN, $$Limit) - 1,
		true,
	},
	{
		(uint32_t)__image2_entry_func__ - 0x20,
#if (defined(SECURE_UART1) && defined(PSA_FF_TEST_SECURE_UART2))
		(UART1_BASE_NS - 1),
		false,
	},
	{
		UART3_BASE_NS,
#elif defined(PSA_FF_TEST_SECURE_UART2)
		(UART2_BASE_NS - 1),
		false,
	},
	{
		UART3_BASE_NS,
#elif defined(SECURE_UART1)
		(UART1_BASE_NS - 1),
		false,
	},
	{
		UART2_BASE_NS,
#endif
		0xFFFFFFFF,
		false,
	},
#if defined(PSA_API_TEST_NS) && !defined(PSA_API_TEST_IPC)
	{
		DEV_APIS_TEST_NVMEM_REGION_START,
		DEV_APIS_TEST_NVMEM_REGION_LIMIT,
		false,
	},
#endif
};

#define NR_SAU_INIT_STEP                 3

FIH_RET_TYPE(int32_t) sau_and_idau_cfg(void)
{
	// struct spctrl_def *spctrl = CMSDK_SPCTRL;
	uint32_t i;

	/* Ensure all memory accesses are completed */
	__DMB();

	/* Enables SAU */
	TZ_SAU_Enable();

	for (i = 0; i < ARRAY_SIZE(sau_cfg); i++) {
		SAU->RNR = i;
		SAU->RBAR = sau_cfg[i].RBAR & SAU_RBAR_BADDR_Msk;
		SAU->RLAR = (sau_cfg[i].RLAR & SAU_RLAR_LADDR_Msk) |
					(sau_cfg[i].nsc ? SAU_RLAR_NSC_Msk : 0U) |
					SAU_RLAR_ENABLE_Msk;
	}

	/* Allows SAU to define the code region as a NSC */
	// spctrl->nsccfg |= NSCCFG_CODENSC;

	/* Ensure the write is completed and flush pipeline */
	__DSB();
	__ISB();

	/* Clean and invalidate D-cache to ensure cache line NS bit tag matches SAU
	 * address attributes. When SAU enables entries covering ranges that were
	 * previously treated as Secure, cache lines retain their original NS bit
	 * tags (set at write time). This tag-SAU mismatch causes imprecise BusFault
	 * when NS code later runs DCISW. Secure DCCISW clears all tags and valid bits.
	 */
	SCB_CleanInvalidateDCache();

	FIH_RET(fih_int_encode(0));
}

#ifdef TFM_FIH_PROFILE_ON
fih_int fih_verify_sau_and_idau_cfg(void)
{
	struct spctrl_def *spctrl = CMSDK_SPCTRL;
	uint32_t i;

	/* Check SAU is enabled */
	if ((SAU->CTRL & (SAU_CTRL_ENABLE_Msk)) != (SAU_CTRL_ENABLE_Msk)) {
		FIH_RET(fih_int_encode(ARM_DRIVER_ERROR));
	}

	for (i = 0; i < ARRAY_SIZE(sau_cfg); i++) {
		SAU->RNR = i;
		if (SAU->RBAR != (sau_cfg[i].RBAR & SAU_RBAR_BADDR_Msk)) {
			FIH_RET(fih_int_encode(ARM_DRIVER_ERROR));
		}
		if (SAU->RLAR != ((sau_cfg[i].RLAR & SAU_RLAR_LADDR_Msk) |
						  (sau_cfg[i].nsc ? SAU_RLAR_NSC_Msk : 0U) |
						  SAU_RLAR_ENABLE_Msk)) {
			FIH_RET(fih_int_encode(ARM_DRIVER_ERROR));
		}
	}

	if ((spctrl->nsccfg & (NSCCFG_CODENSC)) != (NSCCFG_CODENSC)) {
		FIH_RET(fih_int_encode(ARM_DRIVER_ERROR));
	}

	FIH_RET(fih_int_encode(0));
}
#endif /* TFM_FIH_PROFILE_ON */

/*------------------- Memory configuration functions -------------------------*/
#ifdef BL2
#define NR_MPC_INIT_STEP                 7
#else
#define NR_MPC_INIT_STEP                 6
#endif

/*---------------------- PPC configuration functions -------------------------*/
#define NR_PPC_INIT_STEP                 4
