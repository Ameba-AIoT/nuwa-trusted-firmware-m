/*
 * Copyright (c) 2026, Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>

#include "ameba_pm_resume.h"

#include "cmsis.h"
#include "current.h"
#include "load/partition_defs.h"
#include "memory_symbols.h"
#include "runtime_defs.h"
#include "spm.h"
#include "tfm_arch.h"
#include "tfm_core_trustzone.h"
#include "tfm_hal_isolation.h"
#include "tfm_hal_platform.h"
#include "fih.h"
#include "utilities.h"

#include <ameba_soc.h>

/* The NS agent thread entry, branched to rather than scheduled; see below. */
extern void ns_agent_tz_main(uint32_t c_entry);

#define AMEBA_PM_NVIC_REGS      (((MAX_PERIPHERAL_IRQ_NUM) + 31) / 32)
/* SCB->SHPR covers the twelve configurable system handlers. */
#define AMEBA_PM_SHPR_NUM       12

/* The configuration half of SHCSR; the rest is exception status. */
#define AMEBA_PM_SHCSR_FAULT_ENA                                             \
	(SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |             \
	 SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_SECUREFAULTENA_Msk)

/*
 * The core state the power-gate clears, kept across it -- only what cannot be
 * recomputed. The attribution and MPU regions come from the platform's static
 * tables on the way back; the interrupt targets, enables and priorities cannot,
 * the non-secure world setting those up at runtime as it arms its wake-up
 * sources.
 *
 * In .noinit, .bss being zeroed on every path that does not resume.
 */
static struct {
	uint32_t nvic_itns[AMEBA_PM_NVIC_REGS];
	uint32_t nvic_iser[AMEBA_PM_NVIC_REGS];
	uint8_t nvic_ipr[MAX_PERIPHERAL_IRQ_NUM];

	uint32_t scb_aircr;
	uint32_t scb_vtor;
	uint32_t scb_cpacr;
	uint32_t scb_nsacr;
	uint32_t scb_ccr;
	uint32_t scb_shcsr;
	uint8_t scb_shpr[AMEBA_PM_SHPR_NUM];

	uint32_t basepri;
	uint32_t primask;

#if defined(SOC_AMEBAG2)
	/*
	 * AmebaG2 takes its secure exceptions through the ROM's vector table, whose
	 * entries are trampolines into handlers registered with these setters. The
	 * registration lives in the ROM's own RAM, which the power-gate clears, and it
	 * is not reachable through VTOR.
	 */
	void (*svcall_handler)(void);
	void (*pendsv_handler)(void);
#endif

} ameba_pm_core __attribute__((section(".noinit")));

void ameba_pm_core_backup(void)
{
	NVIC_Type *nvic = NVIC;
	uint32_t i;

	ameba_pm_core.basepri = __get_BASEPRI();
	ameba_pm_core.primask = __get_PRIMASK();

	ameba_pm_core.scb_aircr = SCB->AIRCR;
	/*
	 * The coprocessor and fault configuration. The boot sets these up in
	 * SystemInit() and tfm_arch_config_extensions(), neither of which runs on the
	 * wake path -- and CPACR in particular has to be back before any floating-point
	 * instruction, which includes ones the compiler may place in this file's own
	 * callers.
	 */
	ameba_pm_core.scb_cpacr = SCB->CPACR;
	ameba_pm_core.scb_nsacr = SCB->NSACR;
	ameba_pm_core.scb_ccr = SCB->CCR;
	/*
	 * Only the fault enables. The rest of SHCSR is which exceptions are active and
	 * pending, which describes the call this is running inside -- a SysTick and an
	 * SVCall are active here, the sleep having been requested from a thread through
	 * the PSA call ABI. Putting that back on the wake would claim handlers that no
	 * longer have stack frames.
	 */
	ameba_pm_core.scb_shcsr = SCB->SHCSR & AMEBA_PM_SHCSR_FAULT_ENA;
	for (i = 0U; i < AMEBA_PM_SHPR_NUM; i++) {
		ameba_pm_core.scb_shpr[i] = SCB->SHPR[i];
	}
#if defined(__VTOR_PRESENT) && (__VTOR_PRESENT == 1)
	ameba_pm_core.scb_vtor = SCB->VTOR;
#endif

	for (i = 0U; i < AMEBA_PM_NVIC_REGS; i++) {
		/* A set bit in ITNS means the interrupt targets the non-secure state. */
		ameba_pm_core.nvic_itns[i] = nvic->ITNS[i];
		/*
		 * Only the secure interrupts' enables are ours to put back. The
		 * non-secure ones are restored by the non-secure world on its own wake
		 * path, and writing them here would race with it.
		 */
		ameba_pm_core.nvic_iser[i] = nvic->ISER[i] & ~ameba_pm_core.nvic_itns[i];
	}
	/* Priorities for every interrupt, the non-secure ones included. */
	for (i = 0U; i < MAX_PERIPHERAL_IRQ_NUM; i++) {
		ameba_pm_core.nvic_ipr[i] = nvic->IPR[i];
	}

#if defined(SOC_AMEBAG2)
	ameba_pm_core.svcall_handler = (void (*)(void))SVCall_irqfunc_get();
	ameba_pm_core.pendsv_handler = (void (*)(void))PendSV_irqfunc_get();
#endif

	/*
	 * Last, and the reason this has to be the sleep's final secure call: with the
	 * D-cache off, everything written between here and the power-down goes straight
	 * to SRAM. Cleaning instead of disabling would not do -- the writes that matter
	 * most are the ones this call cannot reach, made on the way back out of it, and
	 * those would sit in a cache that does not survive.
	 */
	DCache_Disable();
}

/*
 * Enter the NS agent thread with the stack pointers it expects, by branching --
 * not scheduled, and not returned into by an exception. Its saved context
 * describes a call that had not finished, so the thread is entered at the top the
 * way the boot enters it, needing nothing of the scheduler's state or the saved
 * frame.
 */
__attribute__((naked)) static void ameba_pm_enter_ns_agent(uint32_t c_entry,
							   uint32_t spm_msp)
{
	__ASM volatile(
		/*
		 * Main stack back to the SPM's, then onto PSP for the thread. Here
		 * rather than in the caller, switching stacks mid-C leaving the
		 * compiler addressing locals through a stale pointer. PSP and PSPLIM
		 * are already set; r0 still holds the entry argument.
		 */
		"   msr   msp, r1             \n"
		"   mrs   r2, control         \n"
		"   orr   r2, r2, #2          \n"   /* CONTROL.SPSEL = 1 */
		"   msr   control, r2         \n"
		"   isb                       \n"
		"   b     ns_agent_tz_main    \n"
	);
}

void ameba_pm_core_refill(void)
{
	const struct partition_t *p_ns_agent;
	uintptr_t spm_boundary_unused;
	fih_int fih_rc = FIH_FAILURE;
	NVIC_Type *nvic = NVIC;
	uint32_t aircr = ameba_pm_core.scb_aircr;
	uint32_t vectkey;
	uint32_t metadata_size;
	uint32_t ns_entry;
	uint32_t i;

	DCache_Enable();

	/*
	 * The security attribution and MPU regions, from the platform's static tables
	 * rather than the record. First, because the power-gate leaves the attribution
	 * unit disabled, where everything reads as secure and the first non-secure
	 * access would fault -- including the non-secure world's own stack. The
	 * boundary handed back is discarded, spm_boundary already holding it from the
	 * cold boot.
	 */
	FIH_CALL(tfm_hal_set_up_static_boundaries, fih_rc, &spm_boundary_unused);
	if (fih_not_eq(fih_rc, fih_int_encode(TFM_HAL_SUCCESS))) {
		tfm_core_panic();
	}

	/*
	 * AIRCR, keeping PRIS set: the field is write-protected by a key that reads
	 * back inverted, so the recorded value cannot simply be written back.
	 */
	SCB->CPACR = ameba_pm_core.scb_cpacr;
	SCB->NSACR = ameba_pm_core.scb_nsacr;
	SCB->CCR = ameba_pm_core.scb_ccr;
	SCB->SHCSR = ameba_pm_core.scb_shcsr;
	__DSB();
	__ISB();

	vectkey = (~aircr & SCB_AIRCR_VECTKEYSTAT_Msk);
	SCB->AIRCR = SCB_AIRCR_PRIS_Msk | vectkey | (aircr & ~SCB_AIRCR_VECTKEY_Msk);

	for (i = 0U; i < AMEBA_PM_SHPR_NUM; i++) {
		SCB->SHPR[i] = ameba_pm_core.scb_shpr[i];
	}

	__set_PRIMASK(ameba_pm_core.primask);
	__set_BASEPRI(ameba_pm_core.basepri);

#if defined(__VTOR_PRESENT) && (__VTOR_PRESENT == 1)
	SCB->VTOR = ameba_pm_core.scb_vtor;
#endif
	/*
	 * Attribution first, then priorities, then the enables: IPR is banked by the
	 * attribution, so which bank a write reaches depends on ITNS already being back,
	 * and nothing should become enabled at a priority that has not been put back.
	 */
	for (i = 0U; i < AMEBA_PM_NVIC_REGS; i++) {
		nvic->ITNS[i] = ameba_pm_core.nvic_itns[i];
	}
	__DSB();

	for (i = 0U; i < MAX_PERIPHERAL_IRQ_NUM; i++) {
		nvic->IPR[i] = ameba_pm_core.nvic_ipr[i];
	}

	for (i = 0U; i < AMEBA_PM_NVIC_REGS; i++) {
		nvic->ISER[i] = ameba_pm_core.nvic_iser[i];
	}

#if defined(SOC_AMEBAG2)
	SVCall_irqfunc_set(ameba_pm_core.svcall_handler);
	PendSV_irqfunc_set(ameba_pm_core.pendsv_handler);
#endif

	/*
	 * The NS agent's stack, as the boot left it rather than as the interrupted call
	 * did. The boot allocates the partition's runtime metadata -- the table the PSA
	 * call ABI dispatches through -- from the top of this stack and keeps using what
	 * is below; entering the thread anywhere else would either sit on that table or
	 * give up a frame of stack on every wake.
	 */
	p_ns_agent = GET_CURRENT_COMPONENT();

	metadata_size = sizeof(struct runtime_metadata_t);
	if (!IS_IPC_MODEL(p_ns_agent->p_ldinf)) {
		/* The SFN model keeps a service function table alongside it. */
		metadata_size += sizeof(service_fn_t) * p_ns_agent->p_ldinf->nservices;
	}
	metadata_size = ((metadata_size + 7U) & ~7U) + TFM_STACK_SEALED_SIZE;

	tfm_arch_set_psplim(p_ns_agent->ctx_ctrl.sp_limit);
	__set_PSP(p_ns_agent->ctx_ctrl.sp_base - metadata_size);

	/* MSPLIM before MSP: lowering MSP past a stale limit would fault. */
	tfm_arch_set_msplim(SPM_BOOT_STACK_TOP);

	/*
	 * Into the non-secure world at its reset vector, not at the sleep request's
	 * return. Zephyr power-gates through suspend-to-RAM, and its reset handler is
	 * what restores its caches, System Control Block and vector table before
	 * returning to the sleep's call site; going there directly would skip all of
	 * it.
	 *
	 * Its main stack comes from the vector table rather than from whatever it had
	 * grown to, being re-entered at reset -- and only the secure state can write
	 * it, which its reset handler needs from the first instruction.
	 */
	ns_entry = tfm_hal_get_ns_entry_point();
	__TZ_set_MSP_NS(tfm_hal_get_ns_MSP());
	__DSB();
	__ISB();

	ameba_pm_enter_ns_agent(ns_entry, SPM_BOOT_STACK_BOTTOM);
}
