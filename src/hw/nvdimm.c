// Support for finding and booting from NVDIMM
//
// Copyright (C) 2015  Marc Marí <markmb@redhat.com>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "std/acpi.h"
#include "util.h"
#include "output.h"
#include "stacks.h"
#include "x86.h"
#include "string.h"
#include "bregs.h"
#include "farptr.h"
#include "malloc.h"
#include "nvdimm.h"

void *page_table;

static u32 nvdimm_check(struct nvdimm_addr *NvdimmAddr)
{
    u32 eax;

    // Registers are 32 bits. Pass through stack
    asm volatile(
        ".code64\n"
        "movq %1, %%rdx\n"
        "movq 0x202(%%rdx), %%rax\n"
        "subq $0x53726448, %%rax\n" // Check HdrS signature
        ".code32\n"
        : "=a"(eax)
        : "m"(NvdimmAddr->addr)
        : "edx");

    if (!eax) {
        return 1;
    } else {
        return 0;
    }
}

static void nvdimm_copy(struct nvdimm_addr *NvdimmAddr)
{
    u32 real_addr = 0x10000, prot_addr = 0x100000;

    asm volatile(
        ".code64\n"
        "movq %0, %%rdx\n"
        "xorq %%rbx, %%rbx\n"
        "movb 0x1f1(%%rdx), %%bl\n"
        "addq $1, %%rbx\n"
        "shlq $9, %%rbx\n"
        "movq %%rbx, %%rcx\n" // Setup size
        "movq %%rdx, %%rsi\n" // Address from
        "movq %2, %%rdi\n" // Address to
        "rep movsb\n" // Copy setup section to "real_addr"
        "movq %1, %%rcx\n"
        "subq %%rbx, %%rcx\n" // Kernel size
        "movq %%rdx, %%rsi\n"
        "addq %%rbx, %%rsi\n" // Address from
        "movq %3, %%rdi\n" // Address to
        "rep movsb\n" // Copy rest of the kernel to "prot_addr"
        ".code32\n"
        :
        : "m"(NvdimmAddr->addr), "g"(NvdimmAddr->length),
            "g"(real_addr), "g"(prot_addr)
        : "ebx", "ecx", "edx", "edi", "esi", "memory");
}

void nvdimm_setup(void)
{
    if (!nfit_setup()) {
        dprintf(1, "No NVDIMMs found\n");
        return;
    }

    u64 top_addr = 0x100000000ULL;
    struct nvdimm_addr *NvdimmAddr = nfit_get_pmem_addr();

    int i = 0;
    while(NvdimmAddr[i].addr != 0) {
        if (NvdimmAddr[i].addr + NvdimmAddr[i].length > top_addr) {
            top_addr = NvdimmAddr[i].addr + NvdimmAddr[i].length;
        }

        ++i;
    }

    page_table = gen_identity_page_table(top_addr);

    i = 0;
    while(NvdimmAddr[i].addr != 0) {
        if (NvdimmAddr[i].length > 0x300) {
            if (call64(page_table, (void *)nvdimm_check, (u32)&NvdimmAddr[i])) {
                boot_add_nvdimm(&NvdimmAddr[i], "NVDIMM", 0);
            }
        }
        ++i;
    }
}

void nvdimm_boot(struct nvdimm_addr *NvdimmAddr)
{
    dprintf(1, "Loading kernel from NVDIMM\n");

    u32 real_addr = 0x10000, cmdline_addr = 0x20000;

    call64(page_table, (void *)nvdimm_copy, (u32)NvdimmAddr);

    writel((void *)cmdline_addr, 0);

    // Last configurations
    writeb((void *)real_addr + 0x210, 0xB0);
    writeb((void *)real_addr + 0x211, readb((void *)real_addr + 0x211) | 0x80);
    writel((void *)real_addr + 0x218, 0);
    writel((void *)real_addr + 0x21c, 0);
    writew((void *)real_addr + 0x224, cmdline_addr - real_addr - 0x200);
    writel((void *)real_addr + 0x228, cmdline_addr);

    struct bregs br;
    memset(&br, 0, sizeof(br));
    extern void kernel_stub(void);
    br.ebx = real_addr >> 4;
    br.edx = cmdline_addr - real_addr - 16;
    br.code = SEGOFF(SEG_BIOS, (u32)kernel_stub - BUILD_BIOS_ADDR);

    farcall16big(&br);
}
