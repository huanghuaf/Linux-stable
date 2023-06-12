/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Semidriver Semiconductor
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef __SEMIDRIVE_CRASHDUMP_H__
#define __SEMIDRIVE_CRASHDUMP_H__
#include <linux/device.h>
#include <asm/ptrace.h>

#define ELF_CORE_HEADER_ALIGN   4096
#define CRASHDUMP_MAGIC		"crashdump"
/* This primarily represents number of split ranges due to exclusion */
#define CRASH_MAX_RANGES	16

struct crash_mem_range {
	u64 start, end;
};

/* Backup for crash thread regs */
struct crash_info {
	struct pt_regs regs;
	int die_flag;
};

struct crash_mem {
	unsigned int nr_ranges;
	struct crash_mem_range ranges[CRASH_MAX_RANGES];
};

struct crash_elf_header {
	unsigned char magic[16];
	/* elf header phy addr */
	u64 elf_load_addr;
	/* dump flag */
	int dump_flag;
};

/* Misc data about ram ranges needed to prepare elf headers */
struct crash_elf_data {
	/* Must keep in first to pass to preloader */
	struct crash_elf_header header;
	/* Pointer to elf header */
	void *ehdr;
	/* Pointer to next phdr */
	void *bufp;
	/* record system ram chunk */
	struct crash_mem mem;
	/* sysfs interface */
	struct class crashdump_class;
};

#endif /* __SEMIDRIVE_CRASHDUMP_H__ */

