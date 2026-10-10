/*
 * QEMU NetBSD Virtual Machine Monitor (NVMM) accelerator support for AARCH64
 *
 * Copyright (c) 2023 Ryo Shimizu <ryo@nerv.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "system/address-spaces.h"
#include "system/ioport.h"
#include "qemu/accel.h"
#include "system/hw_accel.h"
#include "system/nvmm.h"
#include "system/cpus.h"
#include "system/runstate.h"
#include "hw/irq.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/queue.h"
#include "migration/blocker.h"
#include "nvmm_arm.h"
#include "cpregs.h"
#include "strings.h"
#include "trace.h"
#include "arm-powerctl.h"
#include "target/arm/trace.h"

#include <sys/sysctl.h>
#include <aarch64/armreg.h>


struct AccelCPUState {
    struct nvmm_vcpu vcpu;
    sigset_t unblock_ipi_mask;
    bool stop;
};

struct NVMMState {
      uint64_t vtimer_offset;
//    QTAILQ_HEAD(, nvmm_sw_breakpoint) nvmm_sw_breakpoints;
} nvmm_aarch64_global_state;

struct nvmm_sreg_match {
	int group;
#define SREG_GROUP_SPR	0
#define SREG_GROUP_TID	1
	int reg;
	uint32_t key;
	uint32_t cp_idx;
};

#define ENCODE_SYSREG(crn, crm, op0, op1, op2)	\
    ENCODE_AA64_CP_REG(op0, op1, crn, crm, op2)

static struct nvmm_sreg_match nvmm_sreg_match[] = {
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_ACTLR_EL1,           ENCODE_SYSREG(  1,  0, 3, 0, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_AFSR0_EL1,           ENCODE_SYSREG(  5,  1, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_AFSR1_EL1,           ENCODE_SYSREG(  5,  1, 3, 0, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_AIDR_EL1,            ENCODE_SYSREG(  0,  0, 3, 1, 7) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_AMAIR_EL1,           ENCODE_SYSREG( 10,  3, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_APDAKEYHI_EL1,       ENCODE_SYSREG(  2,  2, 3, 0, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_APDAKEYLO_EL1,       ENCODE_SYSREG(  2,  2, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_APDBKEYHI_EL1,       ENCODE_SYSREG(  2,  2, 3, 0, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_APDBKEYLO_EL1,       ENCODE_SYSREG(  2,  2, 3, 0, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_APGAKEYHI_EL1,       ENCODE_SYSREG(  2,  3, 3, 0, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_APGAKEYLO_EL1,       ENCODE_SYSREG(  2,  3, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_APIAKEYHI_EL1,       ENCODE_SYSREG(  2,  1, 3, 0, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_APIAKEYLO_EL1,       ENCODE_SYSREG(  2,  1, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_APIBKEYHI_EL1,       ENCODE_SYSREG(  2,  1, 3, 0, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_APIBKEYLO_EL1,       ENCODE_SYSREG(  2,  1, 3, 0, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CCSIDR2_EL1,         ENCODE_SYSREG(  0,  0, 3, 1, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CCSIDR_EL1,          ENCODE_SYSREG(  0,  0, 3, 1, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CLIDR_EL1,           ENCODE_SYSREG(  0,  0, 3, 1, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTFRQ_EL0,          ENCODE_SYSREG( 14,  0, 3, 3, 0) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTKCTL_EL1,         ENCODE_SYSREG( 14,  1, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTPCT_EL0,          ENCODE_SYSREG( 14,  0, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTPS_CTL_EL1,       ENCODE_SYSREG( 14,  2, 3, 7, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTPS_CVAL_EL1,      ENCODE_SYSREG( 14,  2, 3, 7, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTPS_TVAL_EL1,      ENCODE_SYSREG( 14,  2, 3, 7, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTP_CTL_EL0,        ENCODE_SYSREG( 14,  2, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTP_CVAL_EL0,       ENCODE_SYSREG( 14,  2, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTP_TVAL_EL0,       ENCODE_SYSREG( 14,  2, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTVCT_EL0,          ENCODE_SYSREG( 14,  0, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTV_CTL_EL0,        ENCODE_SYSREG( 14,  3, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTV_CVAL_EL0,       ENCODE_SYSREG( 14,  3, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CNTV_TVAL_EL0,       ENCODE_SYSREG( 14,  3, 3, 3, 0) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CONTEXTIDR_EL1,      ENCODE_SYSREG( 13,  0, 3, 0, 1) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CPACR_EL1,           ENCODE_SYSREG(  1,  0, 3, 0, 2) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CSSELR_EL1,          ENCODE_SYSREG(  0,  0, 3, 2, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CTR_EL0,             ENCODE_SYSREG(  0,  0, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_CURRENTEL,           ENCODE_SYSREG(  4,  2, 3, 0, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DAIF,                ENCODE_SYSREG(  4,  2, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGAUTHSTATUS_EL1,   ENCODE_SYSREG(  7, 14, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR0_EL1,         ENCODE_SYSREG(  0,  0, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR10_EL1,        ENCODE_SYSREG(  0, 10, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR11_EL1,        ENCODE_SYSREG(  0, 11, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR12_EL1,        ENCODE_SYSREG(  0, 12, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR13_EL1,        ENCODE_SYSREG(  0, 13, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR14_EL1,        ENCODE_SYSREG(  0, 14, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR15_EL1,        ENCODE_SYSREG(  0, 15, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR1_EL1,         ENCODE_SYSREG(  0,  1, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR2_EL1,         ENCODE_SYSREG(  0,  2, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR3_EL1,         ENCODE_SYSREG(  0,  3, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR4_EL1,         ENCODE_SYSREG(  0,  4, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR5_EL1,         ENCODE_SYSREG(  0,  5, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR6_EL1,         ENCODE_SYSREG(  0,  6, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR7_EL1,         ENCODE_SYSREG(  0,  7, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR8_EL1,         ENCODE_SYSREG(  0,  8, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBCR9_EL1,         ENCODE_SYSREG(  0,  9, 2, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR0_EL1,         ENCODE_SYSREG(  0,  0, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR10_EL1,        ENCODE_SYSREG(  0, 10, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR11_EL1,        ENCODE_SYSREG(  0, 11, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR12_EL1,        ENCODE_SYSREG(  0, 12, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR13_EL1,        ENCODE_SYSREG(  0, 13, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR14_EL1,        ENCODE_SYSREG(  0, 14, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR15_EL1,        ENCODE_SYSREG(  0, 15, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR1_EL1,         ENCODE_SYSREG(  0,  1, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR2_EL1,         ENCODE_SYSREG(  0,  2, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR3_EL1,         ENCODE_SYSREG(  0,  3, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR4_EL1,         ENCODE_SYSREG(  0,  4, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR5_EL1,         ENCODE_SYSREG(  0,  5, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR6_EL1,         ENCODE_SYSREG(  0,  6, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR7_EL1,         ENCODE_SYSREG(  0,  7, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR8_EL1,         ENCODE_SYSREG(  0,  8, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGBVR9_EL1,         ENCODE_SYSREG(  0,  9, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGCLAIMCLR_EL1,     ENCODE_SYSREG(  7,  9, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGCLAIMSET_EL1,     ENCODE_SYSREG(  7,  8, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGDTRRX_EL0,        ENCODE_SYSREG(  0,  5, 2, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGDTR_EL0,          ENCODE_SYSREG(  0,  4, 2, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGPRCR_EL1,         ENCODE_SYSREG(  1,  4, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR0_EL1,         ENCODE_SYSREG(  0,  0, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR10_EL1,        ENCODE_SYSREG(  0, 10, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR11_EL1,        ENCODE_SYSREG(  0, 11, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR12_EL1,        ENCODE_SYSREG(  0, 12, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR13_EL1,        ENCODE_SYSREG(  0, 13, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR14_EL1,        ENCODE_SYSREG(  0, 14, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR15_EL1,        ENCODE_SYSREG(  0, 15, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR1_EL1,         ENCODE_SYSREG(  0,  1, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR2_EL1,         ENCODE_SYSREG(  0,  2, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR3_EL1,         ENCODE_SYSREG(  0,  3, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR4_EL1,         ENCODE_SYSREG(  0,  4, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR5_EL1,         ENCODE_SYSREG(  0,  5, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR6_EL1,         ENCODE_SYSREG(  0,  6, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR7_EL1,         ENCODE_SYSREG(  0,  7, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR8_EL1,         ENCODE_SYSREG(  0,  8, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWCR9_EL1,         ENCODE_SYSREG(  0,  9, 2, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR0_EL1,         ENCODE_SYSREG(  0,  0, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR10_EL1,        ENCODE_SYSREG(  0, 10, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR11_EL1,        ENCODE_SYSREG(  0, 11, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR12_EL1,        ENCODE_SYSREG(  0, 12, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR13_EL1,        ENCODE_SYSREG(  0, 13, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR14_EL1,        ENCODE_SYSREG(  0, 14, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR15_EL1,        ENCODE_SYSREG(  0, 15, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR1_EL1,         ENCODE_SYSREG(  0,  1, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR2_EL1,         ENCODE_SYSREG(  0,  2, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR3_EL1,         ENCODE_SYSREG(  0,  3, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR4_EL1,         ENCODE_SYSREG(  0,  4, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR5_EL1,         ENCODE_SYSREG(  0,  5, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR6_EL1,         ENCODE_SYSREG(  0,  6, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR7_EL1,         ENCODE_SYSREG(  0,  7, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR8_EL1,         ENCODE_SYSREG(  0,  8, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DBGWVR9_EL1,         ENCODE_SYSREG(  0,  9, 2, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DCZID_EL0,           ENCODE_SYSREG(  0,  0, 3, 3, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DIT,                 ENCODE_SYSREG(  4,  2, 3, 3, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DLR_EL0,             ENCODE_SYSREG(  4,  5, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_DSPSR_EL0,           ENCODE_SYSREG(  4,  5, 3, 3, 0) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_ELR_EL1,             ENCODE_SYSREG(  4,  0, 3, 0, 1) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_ESR_EL1,             ENCODE_SYSREG(  5,  2, 3, 0, 0) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_FAR_EL1,             ENCODE_SYSREG(  6,  0, 3, 0, 0) },
//NO_RAW  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_FPCR,                ENCODE_SYSREG(  4,  4, 3, 3, 0) },
//NO_RAW  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_FPSR,                ENCODE_SYSREG(  4,  4, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_GCR_EL1,             ENCODE_SYSREG(  1,  0, 3, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_GMID_EL1,            ENCODE_SYSREG(  0,  0, 3, 1, 4) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AA64AFR0_EL1,     ENCODE_SYSREG(  0,  5, 3, 0, 4) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AA64AFR1_EL1,     ENCODE_SYSREG(  0,  5, 3, 0, 5) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AA64DFR0_EL1,     ENCODE_SYSREG(  0,  5, 3, 0, 0) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AA64DFR1_EL1,     ENCODE_SYSREG(  0,  5, 3, 0, 1) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AA64ISAR0_EL1,    ENCODE_SYSREG(  0,  6, 3, 0, 0) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AA64ISAR1_EL1,    ENCODE_SYSREG(  0,  6, 3, 0, 1) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AA64MMFR0_EL1,    ENCODE_SYSREG(  0,  7, 3, 0, 0) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AA64MMFR1_EL1,    ENCODE_SYSREG(  0,  7, 3, 0, 1) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AA64MMFR2_EL1,    ENCODE_SYSREG(  0,  7, 3, 0, 2) },
//NO_RAW  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AA64PFR0_EL1,     ENCODE_SYSREG(  0,  4, 3, 0, 0) },
//NO_RAW  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AA64PFR1_EL1,     ENCODE_SYSREG(  0,  4, 3, 0, 1) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_AFR0_EL1,         ENCODE_SYSREG(  0,  1, 3, 0, 3) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_DFR0_EL1,         ENCODE_SYSREG(  0,  1, 3, 0, 2) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_DFR1_EL1,         ENCODE_SYSREG(  0,  3, 3, 0, 5) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_ISAR0_EL1,        ENCODE_SYSREG(  0,  2, 3, 0, 0) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_ISAR1_EL1,        ENCODE_SYSREG(  0,  2, 3, 0, 1) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_ISAR2_EL1,        ENCODE_SYSREG(  0,  2, 3, 0, 2) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_ISAR3_EL1,        ENCODE_SYSREG(  0,  2, 3, 0, 3) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_ISAR4_EL1,        ENCODE_SYSREG(  0,  2, 3, 0, 4) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_ISAR5_EL1,        ENCODE_SYSREG(  0,  2, 3, 0, 5) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_ISAR6_EL1,        ENCODE_SYSREG(  0,  2, 3, 0, 7) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_MMFR0_EL1,        ENCODE_SYSREG(  0,  1, 3, 0, 4) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_MMFR1_EL1,        ENCODE_SYSREG(  0,  1, 3, 0, 5) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_MMFR2_EL1,        ENCODE_SYSREG(  0,  1, 3, 0, 6) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_MMFR3_EL1,        ENCODE_SYSREG(  0,  1, 3, 0, 7) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_MMFR4_EL1,        ENCODE_SYSREG(  0,  2, 3, 0, 6) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_MMFR5_EL1,        ENCODE_SYSREG(  0,  3, 3, 0, 6) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_PFR0_EL1,         ENCODE_SYSREG(  0,  1, 3, 0, 0) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_PFR1_EL1,         ENCODE_SYSREG(  0,  1, 3, 0, 1) },
//  { SREG_GROUP_TID, NVMM_AARCH64_TID_ID_PFR2_EL1,         ENCODE_SYSREG(  0,  3, 3, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_ISR_EL1,             ENCODE_SYSREG( 12,  1, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_LORC_EL1,            ENCODE_SYSREG( 10,  4, 3, 0, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_LOREA_EL1,           ENCODE_SYSREG( 10,  4, 3, 0, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_LORID_EL1,           ENCODE_SYSREG( 10,  4, 3, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_LORN_EL1,            ENCODE_SYSREG( 10,  4, 3, 0, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_LORSA_EL1,           ENCODE_SYSREG( 10,  4, 3, 0, 0) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_MAIR_EL1,            ENCODE_SYSREG( 10,  2, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_MDCCINT_EL1,         ENCODE_SYSREG(  0,  2, 2, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_MDCCSR_EL0,          ENCODE_SYSREG(  0,  1, 2, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_MDRAR_EL1,           ENCODE_SYSREG(  1,  0, 2, 0, 0) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_MDSCR_EL1,           ENCODE_SYSREG(  0,  2, 2, 0, 2) },
//NO_RAW  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_MIDR_EL1,            ENCODE_SYSREG(  0,  0, 3, 0, 0) },
//NO_RAW  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_MPIDR_EL1,           ENCODE_SYSREG(  0,  0, 3, 0, 5) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_MVFR0_EL1,           ENCODE_SYSREG(  0,  3, 3, 0, 0) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_MVFR1_EL1,           ENCODE_SYSREG(  0,  3, 3, 0, 1) },
    { SREG_GROUP_TID, NVMM_AARCH64_TID_MVFR2_EL1,           ENCODE_SYSREG(  0,  3, 3, 0, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_NZCV,                ENCODE_SYSREG(  4,  2, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_OSDLR_EL1,           ENCODE_SYSREG(  1,  3, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_OSDTRRX_EL1,         ENCODE_SYSREG(  0,  0, 2, 0, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_OSDTRTX_EL1,         ENCODE_SYSREG(  0,  3, 2, 0, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_OSECCR_EL1,          ENCODE_SYSREG(  0,  6, 2, 0, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_OSLAR_EL1,           ENCODE_SYSREG(  1,  0, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_OSLSR_EL1,           ENCODE_SYSREG(  1,  1, 2, 0, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PAN,                 ENCODE_SYSREG(  4,  2, 3, 0, 3) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PAR_EL1,             ENCODE_SYSREG(  7,  4, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMCCFILTR_EL0,       ENCODE_SYSREG( 14, 15, 3, 3, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMCCNTR_EL0,         ENCODE_SYSREG(  9, 13, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMCEID0_EL0,         ENCODE_SYSREG(  9, 12, 3, 3, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMCEID1_EL0,         ENCODE_SYSREG(  9, 12, 3, 3, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMCNTENCLR_EL0,      ENCODE_SYSREG(  9, 12, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMCNTENSET_EL0,      ENCODE_SYSREG(  9, 12, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMCR_EL0,            ENCODE_SYSREG(  9, 12, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR0_EL0,       ENCODE_SYSREG( 14,  8, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR10_EL0,      ENCODE_SYSREG( 14,  9, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR11_EL0,      ENCODE_SYSREG( 14,  9, 3, 3, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR12_EL0,      ENCODE_SYSREG( 14,  9, 3, 3, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR13_EL0,      ENCODE_SYSREG( 14,  9, 3, 3, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR14_EL0,      ENCODE_SYSREG( 14,  9, 3, 3, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR15_EL0,      ENCODE_SYSREG( 14,  9, 3, 3, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR16_EL0,      ENCODE_SYSREG( 14, 10, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR17_EL0,      ENCODE_SYSREG( 14, 10, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR18_EL0,      ENCODE_SYSREG( 14, 10, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR19_EL0,      ENCODE_SYSREG( 14, 10, 3, 3, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR1_EL0,       ENCODE_SYSREG( 14,  8, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR20_EL0,      ENCODE_SYSREG( 14, 10, 3, 3, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR21_EL0,      ENCODE_SYSREG( 14, 10, 3, 3, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR22_EL0,      ENCODE_SYSREG( 14, 10, 3, 3, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR23_EL0,      ENCODE_SYSREG( 14, 10, 3, 3, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR24_EL0,      ENCODE_SYSREG( 14, 11, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR25_EL0,      ENCODE_SYSREG( 14, 11, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR26_EL0,      ENCODE_SYSREG( 14, 11, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR27_EL0,      ENCODE_SYSREG( 14, 11, 3, 3, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR28_EL0,      ENCODE_SYSREG( 14, 11, 3, 3, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR29_EL0,      ENCODE_SYSREG( 14, 11, 3, 3, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR2_EL0,       ENCODE_SYSREG( 14,  8, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR30_EL0,      ENCODE_SYSREG( 14, 11, 3, 3, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR3_EL0,       ENCODE_SYSREG( 14,  8, 3, 3, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR4_EL0,       ENCODE_SYSREG( 14,  8, 3, 3, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR5_EL0,       ENCODE_SYSREG( 14,  8, 3, 3, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR6_EL0,       ENCODE_SYSREG( 14,  8, 3, 3, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR7_EL0,       ENCODE_SYSREG( 14,  8, 3, 3, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR8_EL0,       ENCODE_SYSREG( 14,  9, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVCNTR9_EL0,       ENCODE_SYSREG( 14,  9, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER0_EL0,      ENCODE_SYSREG( 14, 12, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER10_EL0,     ENCODE_SYSREG( 14, 13, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER11_EL0,     ENCODE_SYSREG( 14, 13, 3, 3, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER12_EL0,     ENCODE_SYSREG( 14, 13, 3, 3, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER13_EL0,     ENCODE_SYSREG( 14, 13, 3, 3, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER14_EL0,     ENCODE_SYSREG( 14, 13, 3, 3, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER15_EL0,     ENCODE_SYSREG( 14, 13, 3, 3, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER16_EL0,     ENCODE_SYSREG( 14, 14, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER17_EL0,     ENCODE_SYSREG( 14, 14, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER18_EL0,     ENCODE_SYSREG( 14, 14, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER19_EL0,     ENCODE_SYSREG( 14, 14, 3, 3, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER1_EL0,      ENCODE_SYSREG( 14, 12, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER20_EL0,     ENCODE_SYSREG( 14, 14, 3, 3, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER21_EL0,     ENCODE_SYSREG( 14, 14, 3, 3, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER22_EL0,     ENCODE_SYSREG( 14, 14, 3, 3, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER23_EL0,     ENCODE_SYSREG( 14, 14, 3, 3, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER24_EL0,     ENCODE_SYSREG( 14, 15, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER25_EL0,     ENCODE_SYSREG( 14, 15, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER26_EL0,     ENCODE_SYSREG( 14, 15, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER27_EL0,     ENCODE_SYSREG( 14, 15, 3, 3, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER28_EL0,     ENCODE_SYSREG( 14, 15, 3, 3, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER29_EL0,     ENCODE_SYSREG( 14, 15, 3, 3, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER2_EL0,      ENCODE_SYSREG( 14, 12, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER30_EL0,     ENCODE_SYSREG( 14, 15, 3, 3, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER3_EL0,      ENCODE_SYSREG( 14, 12, 3, 3, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER4_EL0,      ENCODE_SYSREG( 14, 12, 3, 3, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER5_EL0,      ENCODE_SYSREG( 14, 12, 3, 3, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER6_EL0,      ENCODE_SYSREG( 14, 12, 3, 3, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER7_EL0,      ENCODE_SYSREG( 14, 12, 3, 3, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER8_EL0,      ENCODE_SYSREG( 14, 13, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMEVTYPER9_EL0,      ENCODE_SYSREG( 14, 13, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMINTENCLR_EL1,      ENCODE_SYSREG(  9, 14, 3, 0, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMINTENSET_EL1,      ENCODE_SYSREG(  9, 14, 3, 0, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMOVSCLR_EL0,        ENCODE_SYSREG(  9, 12, 3, 3, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMOVSSET_EL0,        ENCODE_SYSREG(  9, 14, 3, 3, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMSELR_EL0,          ENCODE_SYSREG(  9, 12, 3, 3, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMSWINC_EL0,         ENCODE_SYSREG(  9, 12, 3, 3, 4) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMUSERENR_EL0,       ENCODE_SYSREG(  9, 14, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMXEVCNTR_EL0,       ENCODE_SYSREG(  9, 13, 3, 3, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_PMXEVTYPER_EL0,      ENCODE_SYSREG(  9, 13, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_REVIDR_EL1,          ENCODE_SYSREG(  0,  0, 3, 0, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_RGSR_EL1,            ENCODE_SYSREG(  1,  0, 3, 0, 5) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_RMR_EL1,             ENCODE_SYSREG( 12,  0, 3, 0, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_RNDRRS,              ENCODE_SYSREG(  2,  4, 3, 3, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_RNDR,                ENCODE_SYSREG(  2,  4, 3, 3, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_RVBAR_EL1,           ENCODE_SYSREG( 12,  0, 3, 0, 1) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SCTLR_EL1,           ENCODE_SYSREG(  1,  0, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SCXTNUM_EL0,         ENCODE_SYSREG( 13,  0, 3, 3, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SCXTNUM_EL1,         ENCODE_SYSREG( 13,  0, 3, 0, 7) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SPSEL,               ENCODE_SYSREG(  4,  2, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SPSR_ABT,            ENCODE_SYSREG(  4,  3, 3, 4, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SPSR_EL1,            ENCODE_SYSREG(  4,  0, 3, 0, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SPSR_FIQ,            ENCODE_SYSREG(  4,  3, 3, 4, 3) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SPSR_IRQ,            ENCODE_SYSREG(  4,  3, 3, 4, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SPSR_UND,            ENCODE_SYSREG(  4,  3, 3, 4, 2) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SP_EL0,              ENCODE_SYSREG(  4,  1, 3, 0, 0) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SP_EL1,              ENCODE_SYSREG(  4,  1, 3, 4, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_SSBS,                ENCODE_SYSREG(  4,  2, 3, 3, 6) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_TCO,                 ENCODE_SYSREG(  4,  2, 3, 3, 7) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_TCR_EL1,             ENCODE_SYSREG(  2,  0, 3, 0, 2) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_TEECR32_EL1,         ENCODE_SYSREG(  0,  0, 2, 2, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_TEEHBR32_EL1,        ENCODE_SYSREG(  1,  0, 2, 2, 0) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_TFSRE0_EL1,          ENCODE_SYSREG(  5,  6, 3, 0, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_TFSR_EL1,            ENCODE_SYSREG(  5,  6, 3, 0, 0) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_TPIDRRO_EL0,         ENCODE_SYSREG( 13,  0, 3, 3, 3) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_TPIDR_EL0,           ENCODE_SYSREG( 13,  0, 3, 3, 2) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_TPIDR_EL1,           ENCODE_SYSREG( 13,  0, 3, 0, 4) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_TTBR0_EL1,           ENCODE_SYSREG(  2,  0, 3, 0, 0) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_TTBR1_EL1,           ENCODE_SYSREG(  2,  0, 3, 0, 1) },
//  { SREG_GROUP_SPR, NVMM_AARCH64_SPR_UAO,                 ENCODE_SYSREG(  4,  2, 3, 0, 4) },
    { SREG_GROUP_SPR, NVMM_AARCH64_SPR_VBAR_EL1,            ENCODE_SYSREG( 12,  0, 3, 0, 0) },
};




/* QEMU -> NVMM */
void
nvmm_set_registers(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUArchState *env = cpu_env(cpu); // &cpu->env;

    struct nvmm_machine *mach = get_nvmm_mach();
    AccelCPUState *qcpu = cpu->accel;
    struct nvmm_vcpu *vcpu = &qcpu->vcpu;
    struct nvmm_aarch64_state *state = vcpu->state;
    int i, ret;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

    /* GPRs */
    for (i = 0; i < 32; i++) {
        state->gprs[i] = env->xregs[i];
    }
    state->sprs[NVMM_AARCH64_SPR_SP_EL0] = env->sp_el[0];
    state->sprs[NVMM_AARCH64_SPR_SP_EL1] = env->sp_el[1];
    state->sprs[NVMM_AARCH64_SPR_PC] = env->pc;
    state->sprs[NVMM_AARCH64_SPR_SPSR_EL1] = pstate_read(env);

    /* FPRs */
    for (i = 0; i < 32; i++) {
        memcpy(&state->fprs[i], &env->vfp.zregs[i], sizeof(state->fprs[i]));
    }
    state->sprs[NVMM_AARCH64_SPR_FPCR] = vfp_get_fpcr(env);
    state->sprs[NVMM_AARCH64_SPR_FPSR] = vfp_get_fpsr(env);

    /* SPRs */
    assert(write_cpustate_to_list(arm_cpu, false));
    for (i = 0; i < ARRAY_SIZE(nvmm_sreg_match); i++) {
        if (nvmm_sreg_match[i].cp_idx == -1) {
            continue;
        }
        uint64_t val = arm_cpu->cpreg_values[nvmm_sreg_match[i].cp_idx];
        switch (nvmm_sreg_match[i].group) {
        case SREG_GROUP_SPR:
            state->sprs[nvmm_sreg_match[i].reg] = val;
            break;
        case SREG_GROUP_TID:
            state->tids[nvmm_sreg_match[i].reg] = val;
            break;
        default:
            assert(false);
        }
    }

    //XXXXXXX vtimer

    ret = nvmm_vcpu_setstate(mach, vcpu, NVMM_AARCH64_STATE_ALL);
    if (ret == -1) {
        error_report("NVMM: Failed to set virtual processor context,"
            " error=%d", errno);
    }
}

/* NVMM -> QEMU */
void
nvmm_get_registers(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUArchState *env = cpu_env(cpu);
    struct nvmm_machine *mach = get_nvmm_mach();
    AccelCPUState *qcpu = cpu->accel;
    struct nvmm_vcpu *vcpu = &qcpu->vcpu;
    struct nvmm_aarch64_state *state = vcpu->state;
    int i, ret;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

    ret = nvmm_vcpu_getstate(mach, vcpu, NVMM_AARCH64_STATE_ALL);
    if (ret == -1) {
        error_report("NVMM: Failed to get virtual processor context,"
            " error=%d", errno);
    }

    /* GPRs */
    for (i = 0; i < 32; i++) {
        env->xregs[i] = state->gprs[i];
    }
    env->sp_el[0] = state->sprs[NVMM_AARCH64_SPR_SP_EL0];
    env->sp_el[1] = state->sprs[NVMM_AARCH64_SPR_SP_EL1];
    env->pc = state->sprs[NVMM_AARCH64_SPR_PC];
    pstate_write(env, state->sprs[NVMM_AARCH64_SPR_SPSR_EL1]);

    /* FPRs */
    for (i = 0; i < 32; i++) {
        memcpy(&env->vfp.zregs[i], &state->fprs[i], sizeof(state->fprs[i]));
    }
    vfp_set_fpcr(env, state->sprs[NVMM_AARCH64_SPR_FPCR]);
    vfp_set_fpsr(env, state->sprs[NVMM_AARCH64_SPR_FPSR]);

    /* SPRs */
    for (i = 0; i < ARRAY_SIZE(nvmm_sreg_match); i++) {
        if (nvmm_sreg_match[i].cp_idx == -1) {
            continue;
        }

        uint64_t val;
        switch (nvmm_sreg_match[i].group) {
        case SREG_GROUP_SPR:
            val = state->sprs[nvmm_sreg_match[i].reg];
            break;
        case SREG_GROUP_TID:
            val = state->tids[nvmm_sreg_match[i].reg];
            break;
        default:
            assert(false);
        }
        arm_cpu->cpreg_values[nvmm_sreg_match[i].cp_idx] = val;
    }
    assert(write_list_to_cpustate(arm_cpu));
}


static uint64_t nvmm_vtimer_val_raw(void)
{
    /*
     * mach_absolute_time() returns the vtimer value without the VM
     * offset that we define. Add our own offset on top.
     */
#if 1
    asm volatile ("isb" ::: "memory");
    return reg_cntvct_el0_read();
#else
//    return mach_absolute_time() - nvmm_state->vtimer_offset;
#endif
}

static uint64_t nvmm_vtimer_val(void)
{
#if 0
    if (!runstate_is_running()) {
        /* VM is paused, the vtimer value is in vtimer.vtimer_val */
        return vtimer.vtimer_val;
    }
#endif
    return nvmm_vtimer_val_raw();
}

/*
 * Called before the VCPU is run. We inject events generated by the I/O
 * thread.
 */
static void
nvmm_vcpu_pre_run(CPUState *cpu)
{
//    ARMCPU *arm_cpu = ARM_CPU(cpu);
//    CPUArchState *env = cpu_env(cpu);
    struct nvmm_machine *mach = get_nvmm_mach();
    AccelCPUState *qcpu = cpu->accel;
    struct nvmm_vcpu *vcpu = &qcpu->vcpu;
//    struct nvmm_aarch64_state *state = vcpu->state;

    struct nvmm_vcpu_event *event = vcpu->event;
    bool has_event = false;
    int ret;


    if (!has_event && cpu_test_interrupt(cpu, CPU_INTERRUPT_FIQ)) {
        trace_nvmm_inject_fiq();
        event->type = NVMM_VCPU_EVENT_FIQ;
        has_event = true;
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_FIQ);
    }

    if (!has_event && cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD)) {
        trace_nvmm_inject_irq();
        event->type = NVMM_VCPU_EVENT_IRQ;
        has_event = true;
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_HARD);
    }

    if (has_event) {
        ret = nvmm_vcpu_inject(mach, vcpu);
        if (ret == -1) {
            error_report("NVMM: Failed to inject event,"
                " error=%d", errno);
        }
    }
}

/*
 * Called after the VCPU ran.
 */
static void
nvmm_vcpu_post_run(CPUState *cpu, struct nvmm_vcpu_exit *exit)
{
#if 0
    AccelCPUState *qcpu = cpu->accel;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    uint64_t tpr;

    env->eflags = exit->exitstate.rflags;
    qcpu->int_shadow = exit->exitstate.int_shadow;
    qcpu->int_window_exit = exit->exitstate.int_window_exiting;
    qcpu->nmi_window_exit = exit->exitstate.nmi_window_exiting;

    tpr = exit->exitstate.cr8;
    if (qcpu->tpr != tpr) {
        qcpu->tpr = tpr;
        bql_lock();
        cpu_set_apic_tpr(x86_cpu->apic_state, qcpu->tpr);
        bql_unlock();
    }
#endif
}

/* -------------------------------------------------------------------------- */
static int
nvmm_handle_halted(struct nvmm_machine *mach, CPUState *cpu,
    struct nvmm_vcpu_exit *exit)
{
    int ret = 0;

    bql_lock();
    if (!cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD | CPU_INTERRUPT_FIQ)) {
        cpu->exception_index = EXCP_HLT;
        cpu->halted = true;
        ret = 1;
    }
    bql_unlock();

    return ret;
}


static void
nvmm_wait_for_ipi(CPUState *cpu, struct timespec *ts)
{
    /*
     * Use pselect to sleep so that other threads can IPI us while we're
     * sleeping.
     */
    trace_nvmm_wait_for_ipi(true);
    qatomic_set_mb(&cpu->thread_kicked, false);
    bql_unlock();
    pselect(0, 0, 0, 0, ts, &cpu->accel->unblock_ipi_mask);
    bql_lock();
    trace_nvmm_wait_for_ipi(false);
}

#define CNTCTL_ISTATUS  __BIT(2)
#define CNTCTL_IMASK    __BIT(1)
#define CNTCTL_ENABLE   __BIT(0)


static void
nvmm_wfi(CPUState *cpu)
{
    AccelCPUState *qcpu = cpu->accel;
    struct nvmm_machine *mach = get_nvmm_mach();
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    struct nvmm_vcpu *vcpu = &qcpu->vcpu;
    struct nvmm_aarch64_state *state = vcpu->state;
    int64_t ticks_to_sleep;
    struct timespec ts;
    uint64_t seconds;
    uint64_t nanos;
    uint32_t cntfrq;

    if (cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD | CPU_INTERRUPT_FIQ)) {
        /* Interrupt pending, no need to wait */
        return;
    }

    trace_nvmm_wfi();
    /* ret = */ nvmm_vcpu_getstate(mach, vcpu, NVMM_AARCH64_STATE_SPRS);
#if 0
    uint64_t ctl = state->sprs[NVMM_AARCH64_SPR_CNTV_CTL_EL0];
    if (!(ctl & CNTCTL_ENABLE) || (ctl & CNTCTL_IMASK)) {
        /* Timer disabled or masked, just wait for an IPI. */
        nvmm_wait_for_ipi(cpu, NULL);
        state->sprs[NVMM_AARCH64_SPR_PC] += 4;
        cpu->vcpu_dirty = true;
        return;
    }
#endif
    uint64_t cval = state->sprs[NVMM_AARCH64_SPR_CNTV_CVAL_EL0];
    ticks_to_sleep = cval - nvmm_vtimer_val();
    if (ticks_to_sleep < 0) {
        return;
    }

    cntfrq = gt_cntfrq_period_ns(arm_cpu);
    seconds = muldiv64(ticks_to_sleep, cntfrq, NANOSECONDS_PER_SECOND);
    ticks_to_sleep -= muldiv64(seconds, NANOSECONDS_PER_SECOND, cntfrq);
    nanos = ticks_to_sleep * cntfrq;

    /*
     * Don't sleep for less than the time a context switch would take,
     * so that we can satisfy fast timer requests on the same CPU.
     * Measurements on M1 show the sweet spot to be ~2ms.
     */
    if (!seconds && nanos < (2 * SCALE_MS)) {
        return;
    }

    trace_nvmm_wait_for_ipi_sleep(seconds, nanos);
    ts = (struct timespec) { seconds, nanos };
    nvmm_wait_for_ipi(cpu, &ts);

    state->sprs[NVMM_AARCH64_SPR_PC] += 4;
    cpu->vcpu_dirty = true;
}




static void nvmm_psci_cpu_off(ARMCPU *arm_cpu)
{
    int32_t ret = arm_set_cpu_off(arm_cpu_mp_affinity(arm_cpu));
    assert(ret == QEMU_ARM_POWERCTL_RET_SUCCESS);
}

/*
 * Handle a PSCI call.
 *
 * Returns 0 on success
 *         -1 when the PSCI call is unknown,
 */
static bool nvmm_handle_psci_call(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    uint64_t param[4] = {
        env->xregs[0],
        env->xregs[1],
        env->xregs[2],
        env->xregs[3]
    };
    uint64_t context_id, mpidr;
    bool target_aarch64 = true;
    CPUState *target_cpu_state;
    ARMCPU *target_cpu;
    target_ulong entry;
    int target_el = 1;
    int32_t ret = 0;

    trace_arm_psci_call(param[0], param[1], param[2], param[3],
                        arm_cpu_mp_affinity(arm_cpu));

    switch (param[0]) {
    case QEMU_PSCI_0_2_FN_PSCI_VERSION:
        ret = QEMU_PSCI_VERSION_1_1;
        break;
    case QEMU_PSCI_0_2_FN_MIGRATE_INFO_TYPE:
        ret = QEMU_PSCI_0_2_RET_TOS_MIGRATION_NOT_REQUIRED; /* No trusted OS */
        break;
    case QEMU_PSCI_0_2_FN_AFFINITY_INFO:
    case QEMU_PSCI_0_2_FN64_AFFINITY_INFO:
        mpidr = param[1];

        switch (param[2]) {
        case 0:
            target_cpu_state = arm_get_cpu_by_id(mpidr);
            if (!target_cpu_state) {
                ret = QEMU_PSCI_RET_INVALID_PARAMS;
                break;
            }
            target_cpu = ARM_CPU(target_cpu_state);

            ret = target_cpu->power_state;
            break;
        default:
            /* Everything above affinity level 0 is always on. */
            ret = 0;
        }
        break;
    case QEMU_PSCI_0_2_FN_SYSTEM_RESET:
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        /*
         * QEMU reset and shutdown are async requests, but PSCI
         * mandates that we never return from the reset/shutdown
         * call, so power the CPU off now so it doesn't execute
         * anything further.
         */
        nvmm_psci_cpu_off(arm_cpu);
        break;
    case QEMU_PSCI_0_2_FN_SYSTEM_OFF:
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        nvmm_psci_cpu_off(arm_cpu);
        break;
    case QEMU_PSCI_0_1_FN_CPU_ON:
    case QEMU_PSCI_0_2_FN_CPU_ON:
    case QEMU_PSCI_0_2_FN64_CPU_ON:
        mpidr = param[1];
        entry = param[2];
        context_id = param[3];
        ret = arm_set_cpu_on(mpidr, entry, context_id,
                             target_el, target_aarch64);
        break;
    case QEMU_PSCI_0_1_FN_CPU_OFF:
    case QEMU_PSCI_0_2_FN_CPU_OFF:
        nvmm_psci_cpu_off(arm_cpu);
        break;
    case QEMU_PSCI_0_1_FN_CPU_SUSPEND:
    case QEMU_PSCI_0_2_FN_CPU_SUSPEND:
    case QEMU_PSCI_0_2_FN64_CPU_SUSPEND:
        /* Affinity levels are not supported in QEMU */
        if (param[1] & 0xfffe0000) {
            ret = QEMU_PSCI_RET_INVALID_PARAMS;
            break;
        }
        /* Powerdown is not supported, we always go into WFI */
        env->xregs[0] = 0;
        nvmm_wfi(cpu);
        break;
    case QEMU_PSCI_0_1_FN_MIGRATE:
    case QEMU_PSCI_0_2_FN_MIGRATE:
        ret = QEMU_PSCI_RET_NOT_SUPPORTED;
        break;
    case QEMU_PSCI_1_0_FN_PSCI_FEATURES:
        switch (param[1]) {
        case QEMU_PSCI_0_2_FN_PSCI_VERSION:
        case QEMU_PSCI_0_2_FN_MIGRATE_INFO_TYPE:
        case QEMU_PSCI_0_2_FN_AFFINITY_INFO:
        case QEMU_PSCI_0_2_FN64_AFFINITY_INFO:
        case QEMU_PSCI_0_2_FN_SYSTEM_RESET:
        case QEMU_PSCI_0_2_FN_SYSTEM_OFF:
        case QEMU_PSCI_0_1_FN_CPU_ON:
        case QEMU_PSCI_0_2_FN_CPU_ON:
        case QEMU_PSCI_0_2_FN64_CPU_ON:
        case QEMU_PSCI_0_1_FN_CPU_OFF:
        case QEMU_PSCI_0_2_FN_CPU_OFF:
        case QEMU_PSCI_0_1_FN_CPU_SUSPEND:
        case QEMU_PSCI_0_2_FN_CPU_SUSPEND:
        case QEMU_PSCI_0_2_FN64_CPU_SUSPEND:
        case QEMU_PSCI_1_0_FN_PSCI_FEATURES:
            ret = 0;
            break;
        case QEMU_PSCI_0_1_FN_MIGRATE:
        case QEMU_PSCI_0_2_FN_MIGRATE:
        default:
            ret = QEMU_PSCI_RET_NOT_SUPPORTED;
        }
        break;
    default:
        return false;
    }

    env->xregs[0] = ret;

    return true;
}

int
nvmm_vcpu_loop(CPUState *cpu)
{
    CPUARMState *env = cpu_env(cpu);
    ARMCPU *arm_cpu = env_archcpu(env);
    struct nvmm_machine *mach = get_nvmm_mach();
    AccelCPUState *qcpu = cpu->accel;
    struct nvmm_vcpu *vcpu = &qcpu->vcpu;
    struct nvmm_vcpu_exit *exit = vcpu->exit;
    int ret;

#if 0
    //XXXXXXXXXX: nvmm cannot send multiple events at the same time?
    if (cpu_test_interrupt(cpu, CPU_INTERRUPT_FIQ)) {
        trace_nvmm_inject_fiq();
        vcpu->event->type = NVMM_VCPU_EVENT_FIQ;
        nvmm_vcpu_inject(mach, vcpu);
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_FIQ);
        cpu->halted = false;
    } else if (cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD)) {
        trace_nvmm_inject_irq();
        vcpu->event->type = NVMM_VCPU_EVENT_IRQ;
        nvmm_vcpu_inject(mach, vcpu);
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_HARD);
        cpu->halted = false;
    }
#endif
    if (cpu->halted) {
        cpu->exception_index = EXCP_HLT;
        qatomic_set(&cpu->exit_request, false);
        return 0;
    }

    bql_unlock();
    cpu_exec_start(cpu);

    // XXXNH cf. hvf_vcpu_exec

    /*
     * Inner VCPU loop.
     */
    do {
        if (cpu->vcpu_dirty) {
            nvmm_set_registers(cpu);
            cpu->vcpu_dirty = false;
        }

        if (qcpu->stop) {
            cpu->exception_index = EXCP_INTERRUPT;
            qcpu->stop = false;
            ret = 1;
            break;
        }

        nvmm_vcpu_pre_run(cpu);

        /* Read exit_request before the kernel reads the immediate exit flag */
        if (qatomic_load_acquire(&cpu->exit_request)) {
#if NVMM_USER_VERSION >= 2
            nvmm_vcpu_stop(vcpu);
#else
            qemu_cpu_kick_self();
#endif
        }

        ret = nvmm_vcpu_run(mach, vcpu);
        if (ret == -1) {
            error_report("NVMM: Failed to exec a virtual processor,"
                " error=%d", errno);
            break;
        }

        nvmm_vcpu_post_run(cpu, exit);

        // ret = 0;???
        switch (exit->reason) {
        case NVMM_VCPU_EXIT_NONE:
            break;
        case NVMM_VCPU_EXIT_IRQ:
            // Set vtimer / other in virtual gic
            //switched_level = cpu->device_irq_level ^ run->s.regs.device_irq_level;
            trace_nvmm_exit_irq();
            bql_lock();
            if (exit->exitstate.vtimer) {
                // Gets sets in interrupt_request
                qemu_set_irq(arm_cpu->gt_timer_outputs[GTIMER_VIRT], 1);
            }
            bql_unlock();

            break;
        case NVMM_VCPU_EXIT_STOPPED:
            /*
             * The kernel cleared the immediate exit flag; cpu->exit_request
             * must be cleared after
             */
            smp_wmb();
            qcpu->stop = true;
            break;
        case NVMM_VCPU_EXIT_MEMORY:
            ret = nvmm_handle_mem(mach, vcpu);
            break;
        case NVMM_VCPU_EXIT_MRS:
            error_report("XXXXXX: NOTYET: NVMM_VCPU_EXIT_MRS");
            abort();
            break;
        case NVMM_VCPU_EXIT_MSR:
            error_report("XXXXXX: NOTYET: NVMM_VCPU_EXIT_MSR");
            abort();
            break;
        case NVMM_VCPU_EXIT_HVC:
            cpu_synchronize_state(cpu);
            if (arm_cpu->psci_conduit == QEMU_PSCI_CONDUIT_HVC) {
                /* Do NOT advance $pc for HVC */
                if (!nvmm_handle_psci_call(cpu)) {
                    trace_nvmm_unknown_hvc(env->pc, env->xregs[0]);
                    /* SMCCC 1.3 section 5.2 says every unknown SMCCC call returns -1 */
                    env->xregs[0] = -1;
                } else {
                    error_report("XXX handled HVC");
                }
                cpu->vcpu_dirty = true;
            } else {
                error_report("XXX unknown conduit %d", arm_cpu->psci_conduit);
//                trace_nvmm_unknown_hvc(env->pc, env->xregs[0]);
//                nvmm_raise_exception(cpu, EXCP_UDEF, syn_uncategorized(), 1);
            }
            break;
        case NVMM_VCPU_EXIT_SMC:
            error_report("XXXXXX: NOTYET: NVMM_VCPU_EXIT_SMC");
            abort();
            break;
        case NVMM_VCPU_EXIT_WFI:
            bql_lock();
            nvmm_wfi(cpu);
            bql_unlock();
            break;
        case NVMM_VCPU_EXIT_WFE:
            error_report("XXXXXX: NOTYET: NVMM_VCPU_EXIT_WFE");
            break;
        case NVMM_VCPU_EXIT_HALTED:
            ret = nvmm_handle_halted(mach, cpu, exit);
            break;
        case NVMM_VCPU_EXIT_SHUTDOWN:
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            // XXXNH wtf?
            cpu->exception_index = EXCP_INTERRUPT;
            ret = 1;
            break;
        // XXXNH
        // case NVMM_VCPU_EXIT_PPI:
        //         qemu_set_irq(arm_cpu->gt_timer_outputs[GTIMER_VIRT], 1);
        //
        default:
            error_report("NVMM: Unexpected VM exit code"
                " 0x%lx [esr=0x%lx]",
                exit->reason, exit->esr);
            nvmm_get_registers(cpu);
            bql_lock();
            qemu_system_guest_panicked(cpu_get_crash_info(cpu));
            bql_unlock();
            ret = -1;
            break;
        }
    } while (ret == 0);

    cpu_exec_end(cpu);
    bql_lock();

    return ret < 0;
}

/* -------------------------------------------------------------------------- */

static Error *nvmm_migration_blocker;

/*
 * The nvmm_vcpu_stop() mechanism breaks races between entering the VMM
 * and another thread signaling the vCPU thread to exit.
 */

void
nvmm_ipi_signal(int sigcpu)
{
    if (current_cpu) {
        AccelCPUState *qcpu = current_cpu->accel;
        struct nvmm_vcpu *vcpu = &qcpu->vcpu;
        nvmm_vcpu_stop(vcpu);
    }
}


void
nvmm_init_cpu_signals(void)
{
    struct sigaction sigact;
    sigset_t set;

    /* Install the IPI handler. */
    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = nvmm_ipi_signal;
    sigaction(SIG_IPI, &sigact, NULL);

    /* Allow IPIs on the current thread. */
    sigprocmask(SIG_BLOCK, NULL, &set);
    sigdelset(&set, SIG_IPI);
    pthread_sigmask(SIG_SETMASK, &set, NULL);
}

static void
nvmm_sreg_init(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUArchState *env = cpu_env(cpu);
//    AccelCPUState *qcpu = cpu->accel;
    uint32_t sregs_match_len = ARRAY_SIZE(nvmm_sreg_match);
    uint32_t sregs_cnt = 0;

    env->aarch64 = true;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(arm_cpu->gt_cntfrq_hz));

    /* Allocate enough space for our sysreg sync */
    arm_cpu->cpreg_indexes = g_renew(uint64_t, arm_cpu->cpreg_indexes,
                                     sregs_match_len);
    arm_cpu->cpreg_values = g_renew(uint64_t, arm_cpu->cpreg_values,
                                    sregs_match_len);
    arm_cpu->cpreg_vmstate_indexes = g_renew(uint64_t,
                                             arm_cpu->cpreg_vmstate_indexes,
                                             sregs_match_len);
    arm_cpu->cpreg_vmstate_values = g_renew(uint64_t,
                                            arm_cpu->cpreg_vmstate_values,
                                            sregs_match_len);

    memset(arm_cpu->cpreg_values, 0, sregs_match_len * sizeof(uint64_t));

    /* Populate cp list for all known sysregs */
    for (int i = 0; i < sregs_match_len; i++) {
        const ARMCPRegInfo *ri;
        uint32_t key = nvmm_sreg_match[i].key;

        ri = get_arm_cp_reginfo(arm_cpu->cp_regs, key);
        if (ri) {
            assert(!(ri->type & ARM_CP_NO_RAW));
            nvmm_sreg_match[i].cp_idx = sregs_cnt;
            arm_cpu->cpreg_indexes[sregs_cnt++] = cpreg_to_kvm_id(key);
        } else {
            nvmm_sreg_match[i].cp_idx = -1;
        }
    }
    arm_cpu->cpreg_array_len = sregs_cnt;
    arm_cpu->cpreg_vmstate_array_len = sregs_cnt;

    assert(write_cpustate_to_list(arm_cpu, false));
}


int
nvmm_arch_init(void)
{
//    nvmm_aarch64_global_state->vtimer_offset = mach_absolute_time();
//    vmstate_register(NULL, 0, &vmstate_nvmm_vtimer, &vtimer);
//    qemu_add_vm_change_state_handler(nvmm_vm_state_change, &vtimer);

//    nvmm_arm_init_debug();

    return 0;
}


typedef struct ARMHostCPUFeatures {
    ARMISARegisters isar;
    uint64_t features;
    uint64_t midr;
    uint32_t reset_sctlr;
    const char *dtb_compatible;
} ARMHostCPUFeatures;

static ARMHostCPUFeatures arm_host_cpu_features;

void
nvmm_arm_set_cpu_features_from_host(ARMCPU *cpu)
{
    if (arm_host_cpu_features.dtb_compatible == NULL) {
        char path[128];
        size_t len;
        uint64_t sysctlbuf[64];
        struct aarch64_sysctl_cpu_id *id =
            (struct aarch64_sysctl_cpu_id *)sysctlbuf;

        /*
         * XXX: It is necessary to get "machdep.cpuN.cpu_id" of all CPUs,
         * and select the minimum feature.
         */
        snprintf(path, sizeof(path), "machdep.cpu%d.cpu_id", 0);
        len = sizeof(sysctlbuf);
        memset(sysctlbuf, 0, len);
        if (sysctlbyname(path, id, &len, 0, 0) == -1) {
            error_report("couldn't get %s", path);
            return;
        }
        if (len != sizeof(struct aarch64_sysctl_cpu_id)) {
            error_report("Warning: kernel version bumped?");
            return;
        }

        arm_host_cpu_features.midr = id->ac_midr;
        arm_host_cpu_features.isar.idregs[ID_AA64DFR0_EL1_IDX] = id->ac_aa64dfr0;
        arm_host_cpu_features.isar.idregs[ID_AA64DFR1_EL1_IDX] = id->ac_aa64dfr1;
        arm_host_cpu_features.isar.idregs[ID_AA64ISAR0_EL1_IDX] = id->ac_aa64isar0;
        arm_host_cpu_features.isar.idregs[ID_AA64ISAR1_EL1_IDX] = id->ac_aa64isar1;
        arm_host_cpu_features.isar.idregs[ID_AA64MMFR0_EL1_IDX] = id->ac_aa64mmfr0;
        arm_host_cpu_features.isar.idregs[ID_AA64MMFR1_EL1_IDX] = id->ac_aa64mmfr1;
        arm_host_cpu_features.isar.idregs[ID_AA64MMFR2_EL1_IDX] = id->ac_aa64mmfr2;
        arm_host_cpu_features.isar.idregs[ID_AA64PFR0_EL1_IDX] = id->ac_aa64pfr0;
        arm_host_cpu_features.isar.idregs[ID_AA64PFR1_EL1_IDX] = id->ac_aa64pfr1;
        arm_host_cpu_features.isar.idregs[ID_AA64ZFR0_EL1_IDX] = id->ac_aa64zfr0;
        arm_host_cpu_features.isar.idregs[MVFR0_EL1_IDX] = id->ac_mvfr0;
        arm_host_cpu_features.isar.idregs[MVFR1_EL1_IDX] = id->ac_mvfr1;
        arm_host_cpu_features.isar.idregs[MVFR2_EL1_IDX] = id->ac_mvfr2;

        arm_host_cpu_features.isar.idregs[ID_AA64SMFR0_EL1_IDX] = 0;
        arm_host_cpu_features.isar.reset_pmcr_el0 = 0;
        arm_host_cpu_features.reset_sctlr = SCTLR_RES1;

        arm_host_cpu_features.dtb_compatible = "arm,arm-v8";
        arm_host_cpu_features.features = (1ULL << ARM_FEATURE_V8) |
                                         (1ULL << ARM_FEATURE_NEON) |
                                         (1ULL << ARM_FEATURE_AARCH64) |
//                                       (1ULL << ARM_FEATURE_PMU) |
                                         (1ULL << ARM_FEATURE_GENERIC_TIMER);
    }

    /* arm_host_cpu_features to cpu */
    cpu->dtb_compatible = arm_host_cpu_features.dtb_compatible;
    cpu->isar = arm_host_cpu_features.isar;
    cpu->env.features = arm_host_cpu_features.features;
    cpu->midr = arm_host_cpu_features.midr;
    cpu->reset_sctlr = arm_host_cpu_features.reset_sctlr;
}

int
nvmm_init_vcpu(CPUState *cpu)
{
    struct nvmm_machine *mach = get_nvmm_mach();
    Error *local_error = NULL;
    AccelCPUState *qcpu;
    int ret, err;

    nvmm_init_cpu_signals();

    if (nvmm_migration_blocker == NULL) {
        error_setg(&nvmm_migration_blocker,
            "NVMM: Migration not supported");

        if (migrate_add_blocker(&nvmm_migration_blocker, &local_error) < 0) {
            error_report_err(local_error);
            error_free(nvmm_migration_blocker);
            return -EINVAL;
        }
    }

    qcpu = g_malloc0(sizeof(*qcpu));
    if (qcpu == NULL) {
        error_report("NVMM: Failed to allocate VCPU context.");
        return -ENOMEM;
    }

    ret = nvmm_vcpu_create(mach, cpu->cpu_index, &qcpu->vcpu);
    if (ret == -1) {
        err = errno;
        error_report("NVMM: Failed to create a virtual processor,"
            " error=%d", err);
        g_free(qcpu);
        return -err;
    }

    ret = nvmm_vcpu_configure(mach, &qcpu->vcpu, NVMM_VCPU_CONF_CALLBACKS,
        &nvmm_callbacks);
    if (ret == -1) {
        err = errno;
        error_report("NVMM: Failed to configure a virtual processor,"
            " error=%d", err);
        g_free(qcpu);
        return -err;
    }

    cpu->vcpu_dirty = true;
    cpu->accel = qcpu;

    // XXXNH maybe not, cf. nvmm_init_cpu_signals above
    pthread_sigmask(SIG_BLOCK, NULL, &cpu->accel->unblock_ipi_mask);
    sigdelset(&cpu->accel->unblock_ipi_mask, SIG_IPI);

    nvmm_sreg_init(cpu);

    /* Set CP_NO_RAW system registers on init */
    struct nvmm_aarch64_state *state = qcpu->vcpu.state;
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    ret = nvmm_vcpu_getstate(mach, &qcpu->vcpu, NVMM_AARCH64_STATE_ALL);

    state->sprs[NVMM_AARCH64_SPR_MIDR_EL1] = arm_cpu->midr;
    state->sprs[NVMM_AARCH64_SPR_MPIDR_EL1] = (1U << 31) | arm_cpu->mp_affinity;

//    CPUArchState *env = &arm_cpu->env;
#if 0
    uint64_t pfr = state->tids[NVMM_AARCH64_TID_ID_AA64PFR0_EL1];

    pfr &= ~ID_AA64PFR0_EL1_GIC;
    pfr |= __SHIFTIN(env->gicv3state ? 1 : 0, ID_AA64PFR0_EL1_GIC);
    state->tids[NVMM_AARCH64_TID_ID_AA64PFR0_EL1] = pfr;
#endif

    // ArmVirtQemu 202605 needs this
    arm_cpu->psci_version = QEMU_PSCI_VERSION_0_2;

    /* We're limited to underlying hardware caps, override internal versions */
    arm_cpu->isar.idregs[ID_AA64MMFR0_EL1_IDX] = state->tids[NVMM_AARCH64_TID_ID_AA64MMFR0_EL1];

    return 0;
}

void
nvmm_destroy_vcpu(CPUState *cpu)
{
    struct nvmm_machine *mach = get_nvmm_mach();
    AccelCPUState *qcpu = cpu->accel;

    nvmm_vcpu_destroy(mach, &qcpu->vcpu);
}
