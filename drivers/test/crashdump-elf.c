/*
 * Copyright (c) 2022, Semidriver Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <linux/cpumask.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/kexec.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/kexec.h>
#include <linux/kdebug.h>

#define ELF_CORE_HEADER_ALIGN   4096

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

/* Misc data about ram ranges needed to prepare elf headers */
struct crash_elf_data {
	/* Pointer to elf header */
	void *ehdr;
	/* Pointer to next phdr */
	void *bufp;
	/* elf header phy addr */
	phys_addr_t elf_load_addr;
	/* record system ram chunk */
	struct crash_mem mem;
};

static struct crash_elf_data *ced;
static DEFINE_PER_CPU(struct crash_info, crash_info);

#ifdef CONFIG_ARM64
#define printk_phdr(prefix, phdr)						\
do {										\
	pr_debug("%s: p_type = %u, p_offset = 0x%llx p_paddr = 0x%llx "	\
		"p_vaddr = 0x%llx p_filesz = 0x%llx p_memsz = 0x%llx\n",	\
		(prefix), (phdr)->p_type,					\
		(unsigned long long)((phdr)->p_offset),				\
		(unsigned long long)((phdr)->p_paddr),				\
		(unsigned long long)((phdr)->p_vaddr),				\
		(unsigned long long)((phdr)->p_filesz),				\
		(unsigned long long)((phdr)->p_memsz));				\
} while(0)
#else
#define printk_phdr(prefix, phdr)					\
do {									\
	pr_debug("%s: p_type = %u, p_offset = 0x%x " "p_paddr = 0x%x "	\
		"p_vaddr = 0x%x p_filesz = 0x%x p_memsz = 0x%x\n",	\
		(prefix), (phdr)->p_type, (phdr)->p_offset, (phdr)->p_paddr, \
		(phdr)->p_vaddr, (phdr)->p_filesz, (phdr)->p_memsz);	\
} while(0)
#endif

static int crashdump_panic_handler(struct notifier_block *this,
				unsigned long event, void *unused)
{
	unsigned int cpu;
	struct crash_info *info;

	for_each_present_cpu(cpu) {
		info = per_cpu_ptr(&crash_info, cpu);
		if (info->die_flag) {
			crash_save_cpu(&info->regs, cpu);
		}
	}

	return NOTIFY_OK;
}

static int crashdump_die_handler(struct notifier_block *this,
				unsigned long event, void *unused)
{
	unsigned int this_cpu;
	struct die_args *args = (struct die_args *)unused;
	struct crash_info *info;

	this_cpu = raw_smp_processor_id();

	info = per_cpu_ptr(&crash_info, this_cpu);

	info->die_flag = 1;
	crash_setup_regs(&info->regs, args->regs);
	crash_save_cpu(&info->regs, this_cpu);

	return NOTIFY_OK;

}

static struct notifier_block crashdump_panic_event_nb = {
	.notifier_call	= crashdump_panic_handler,
	.priority	= 0x0,	/* we need to be notified last */
};

static struct notifier_block crashdump_die_event_nb = {
	.notifier_call	= crashdump_die_handler,
	.priority	= 0x0,	/* we need to be notified last */
};

static int get_nr_ram_ranges_callback(u64 start, u64 end, void *arg)
{
	struct crash_elf_data *ced = arg;
	struct crash_mem *cmem = &ced->mem;
	unsigned int nr_ranges = cmem->nr_ranges;

	if (nr_ranges >= CRASH_MAX_RANGES) {
		pr_err("Too many crash ranges\n");
		return -ENOMEM;
	}

	cmem->ranges[nr_ranges].start = start;
	cmem->ranges[nr_ranges].end = end;

	cmem->nr_ranges++;
	return 0;
}

/* Gather all the required information to prepare elf headers for ram regions */
static void fill_up_crash_elf_data(struct crash_elf_data *ced)
{
	walk_system_ram_res(0, -1, ced,
				get_nr_ram_ranges_callback);
}

static int prepare_elf_headers(struct crash_elf_data *ced)
{
	struct elfhdr *ehdr;
	struct elf_phdr *phdr;
	unsigned long nr_phdr, elf_sz;
	unsigned long nr_cpus = num_possible_cpus();
	unsigned char *buf, *bufp;
	unsigned long long notes_addr;
	unsigned long long start, end;
	unsigned int cpu;
	struct crash_mem *mem = &ced->mem;
	int i;
	struct page *page = NULL;

	/* extra phdr for vmcoreinfo elf note */
	nr_phdr = nr_cpus + 1;
	nr_phdr += mem->nr_ranges;

	elf_sz = sizeof(struct elfhdr) + nr_phdr * sizeof(struct elf_phdr);
	elf_sz = ALIGN(elf_sz, ELF_CORE_HEADER_ALIGN);

	buf = kzalloc(elf_sz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	bufp = buf;
	ehdr = (struct elfhdr *)bufp;
	bufp += sizeof(struct elfhdr);
	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELF_CLASS;
	ehdr->e_ident[EI_DATA] = ELF_DATA;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELF_OSABI;
	memset(ehdr->e_ident + EI_PAD, 0, EI_NIDENT - EI_PAD);
	ehdr->e_type = ET_CORE;
	ehdr->e_machine = ELF_ARCH;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_phoff = sizeof(struct elfhdr);
	ehdr->e_ehsize = sizeof(struct elfhdr);
	ehdr->e_phentsize = sizeof(struct elf_phdr);

	/* Prepare one phdr of type PT_NOTE for each present cpu */
	for_each_present_cpu(cpu) {
		phdr = (struct elf_phdr *)bufp;
		bufp += sizeof(struct elf_phdr);
		phdr->p_type = PT_NOTE;
		notes_addr = per_cpu_ptr_to_phys(per_cpu_ptr(crash_notes, cpu));
		phdr->p_offset = phdr->p_paddr = notes_addr;
		phdr->p_filesz = phdr->p_memsz = sizeof(note_buf_t);
		(ehdr->e_phnum)++;
		printk_phdr("Elf header", phdr);
	}

	/* Prepare one PT_NOTE header for vmcoreinfo */
	phdr = (struct elf_phdr *)bufp;
	bufp += sizeof(struct elf_phdr);
	phdr->p_type = PT_NOTE;
	phdr->p_offset = phdr->p_paddr = paddr_vmcoreinfo_note();
	phdr->p_filesz = phdr->p_memsz = VMCOREINFO_NOTE_SIZE;
	(ehdr->e_phnum)++;
	printk_phdr("vmcoreinfo header", phdr);

	/* Prepare PT_LOAD headers for system ram chunks. */
	for (i = 0; i < mem->nr_ranges; i++) {
		start = mem->ranges[i].start;;
		end = mem->ranges[i].end;
		phdr = (struct elf_phdr *)bufp;
		bufp += sizeof(struct elf_phdr);

		phdr->p_type = PT_LOAD;
		phdr->p_flags = PF_R|PF_W|PF_X;
		phdr->p_offset  = start;

		phdr->p_paddr = start;
#ifdef CONFIG_ARM64
		phdr->p_vaddr = (u64)page_to_virt(phys_to_page(start));
#else
		phdr->p_vaddr = (u32)page_to_virt(phys_to_page(start));
#endif
		phdr->p_filesz = phdr->p_memsz = end - start + 1;
		phdr->p_align = 0;
		(ehdr->e_phnum)++;
		printk_phdr("Elf header", phdr);
	}

	page = virt_to_page(buf);
	if (IS_ERR_OR_NULL(page)) {
		pr_err("virt_to_page() error\n");
		return -EINVAL;
	}

	ced->elf_load_addr = page_to_phys(page);

	ced->ehdr = ehdr;
	ced->bufp = bufp;

	return 0;
}

static int crashdump_elf_initcall(void)
{
	int ret;

	ced = (struct crash_elf_data *)kzalloc(sizeof(struct crash_elf_data), GFP_KERNEL);
	if (!ced)
		return -ENOMEM;

	fill_up_crash_elf_data(ced);

	ret = prepare_elf_headers(ced);
	if (ret) {
		pr_err("prepare elf headers error\n");
		return ret;
	}

	crash_save_vmcoreinfo();

	ret = register_die_notifier(&crashdump_die_event_nb);
	if (ret) {
		pr_err("%s, failed to register die notifier\n", __func__);
		return ret;
	}

	ret = atomic_notifier_chain_register(&panic_notifier_list,
				       &crashdump_panic_event_nb);
	if (ret) {
		pr_err("%s, failed to register panic notifier\n", __func__);
		return ret;
	}

	pr_err("finish prepare elfcore header\n");
	return 0;
}

/* must be after the kexec_core.c module */
subsys_initcall_sync(crashdump_elf_initcall);

MODULE_DESCRIPTION("ramdump elf driver");
MODULE_AUTHOR("Huafeng huang");
MODULE_LICENSE("GPL v2");
