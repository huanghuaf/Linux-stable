/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2012 ARM Limited
 */

#ifndef _LINUX_VEXPRESS_ION_H
#define _LINUX_VEXPRESS_ION_H

#ifdef CONFIG_ION_VEXPRESS

struct ion_client *vexpress_ion_client_create(char *name);

#else

struct ion_client *vexpress_ion_client_create(char *name)
{
	return NULL;
}
#endif
#endif
