
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
#include <linux/io.h>
#include <linux/kexec.h>
#include <linux/kdebug.h>
#include <linux/cma.h>
#include <linux/dma-contiguous.h>
#include <linux/highmem.h>
#include "crashdump.h"

static struct crash_elf_data *ced;
int __read_mostly dump_flag = 1;
static DEFINE_PER_CPU(struct crash_info, crash_info);

#ifdef CONFIG_ARM64
#define printk_phdr(prefix, phdr)					\
do {									\
	pr_debug("%s: p_type = %u, p_offset = 0x%llx p_paddr = 0x%llx",	\
		(prefix), (phdr)->p_type,				\
		(unsigned long long)((phdr)->p_offset),			\
		(unsigned long long)((phdr)->p_paddr));			\
	pr_debug(" p_vaddr = 0x%llx p_filesz = 0x%llx p_memsz = 0x%llx\n",\
		(unsigned long long)((phdr)->p_vaddr),			\
		(unsigned long long)((phdr)->p_filesz),			\
		(unsigned long long)((phdr)->p_memsz));			\
} while(0)
#else
#define printk_phdr(prefix, phdr)					\
do {									\
	pr_debug("%s: p_type = %u, p_offset = 0x%x p_paddr = 0x%x",	\
		(prefix), (phdr)->p_type, (phdr)->p_offset, (phdr)->p_paddr);\
	pr_debug(" p_vaddr = 0x%x p_filesz = 0x%x p_memsz = 0x%x\n",	\
		(phdr)->p_vaddr, (phdr)->p_filesz, (phdr)->p_memsz);	\
} while (0)
#endif

core_param(crashdump, dump_flag, int, 0644);

static ssize_t store_enable(struct class *class, struct class_attribute *attr,
		const char *buf, size_t count)
{
	struct crash_elf_data *ced = container_of(class, struct crash_elf_data,
						  crashdump_class);
	int val;
	int ret;


	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	if (val) {
		dump_flag = 1;
		ced->header.dump_flag = 1;
	} else {
		dump_flag = 0;
		ced->header.dump_flag = 0;
	}

	return count;
}

static ssize_t show_enable(struct class *class, struct class_attribute *attr,
			char *buf)
{
	struct crash_elf_data *ced = container_of(class, struct crash_elf_data,
						  crashdump_class);

	if (ced->header.dump_flag > 0)
		return snprintf(buf, 8, "enable\n");
	else
		return snprintf(buf, 9, "disable\n");
}

static struct class_attribute crashdump_class_attrs[] = {
	__ATTR(enable, 0644, show_enable, store_enable),
	__ATTR_NULL,
};

static int crashdump_panic_handler(struct notifier_block *this,
				unsigned long event, void *unused)
{
	unsigned int cpu;
	struct crash_info *info;
	unsigned int this_cpu;

#ifdef CONFIG_ARM
	/* arm64 not support now */
	crash_smp_send_stop();
#endif

	this_cpu = raw_smp_processor_id();

	for_each_present_cpu(cpu) {
		info = per_cpu_ptr(&crash_info, cpu);
		if (info->die_flag)
			crash_save_cpu(&info->regs, cpu);
		else {
			if (cpu == this_cpu) {
				crash_setup_regs(&info->regs, NULL);
				crash_save_cpu(&info->regs, cpu);
			}
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
	struct page *pages = NULL;

	/* extra phdr for vmcoreinfo elf note */
	nr_phdr = nr_cpus + 1;
	nr_phdr += mem->nr_ranges;

	elf_sz = sizeof(struct elfhdr) + nr_phdr * sizeof(struct elf_phdr);
	elf_sz = ALIGN(elf_sz, ELF_CORE_HEADER_ALIGN);

	pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, get_order(elf_sz));
	if (IS_ERR_OR_NULL(pages))
		return -ENOMEM;

	buf = page_to_virt(pages);

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

	ced->header.elf_load_addr = page_to_phys(pages);

	ced->ehdr = ehdr;
	ced->bufp = bufp;

	return 0;
}

static int crashdump_elf_initcall(void)
{
	int ret;
	phys_addr_t ced_addr;
	struct page *pages = NULL;
	unsigned long size = PAGE_ALIGN(sizeof(struct crash_elf_data));
	unsigned long nr_pages = size >> PAGE_SHIFT;
	unsigned long align = get_order(size);
	int cma_alloc_fail = 0;


	/* use cma to keep phyaddr below 4G for ap1/ap2 */
	pages = cma_alloc(dev_get_cma_area(NULL), nr_pages, align);
	if (IS_ERR_OR_NULL(pages)) {
		/* try again */
		cma_alloc_fail = 1;
		pages = alloc_pages(GFP_KERNEL | __GFP_ZERO | GFP_DMA32,
				  get_order(sizeof(struct crash_elf_data)));
		if (IS_ERR_OR_NULL(pages))
			return -ENOMEM;
	}

	/* clear the page  from cma */
	if (!cma_alloc_fail) {
		if (PageHighMem(pages)) {
			unsigned long nr_clear_pages = nr_pages;
			struct page *page = pages;

			while (nr_clear_pages > 0) {
				void *vaddr = kmap_atomic(page);

				memset(vaddr, 0, PAGE_SIZE);
				kunmap_atomic(vaddr);
				page++;
				nr_clear_pages--;
			}
		} else {
			memset(page_address(pages), 0, size);
		}
	}

	ced = (struct crash_elf_data *)page_to_virt(pages);
	ced_addr = page_to_phys(pages);

	memcpy(ced->header.magic, CRASHDUMP_MAGIC, strlen(CRASHDUMP_MAGIC));
	ced->header.dump_flag = dump_flag;

	ced->crashdump_class.name = "crashdump_class";
	ced->crashdump_class.owner = THIS_MODULE;
	ced->crashdump_class.class_attrs = crashdump_class_attrs;
	ret = class_register(&(ced->crashdump_class));
	if (ret < 0) {
		pr_err("register crashdump_class class failed: %d\n", ret);
		ced->crashdump_class.name = NULL;
	}

	fill_up_crash_elf_data(ced);

	ret = prepare_elf_headers(ced);
	if (ret) {
		pr_err("prepare elf headers error\n");
		if (ced->crashdump_class.name)
			class_unregister(&(ced->crashdump_class));
		if (cma_alloc_fail)
			__free_pages(pages,
				     get_order(sizeof(struct crash_elf_data)));
		else
			cma_release(dev_get_cma_area(NULL), pages, nr_pages);
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
