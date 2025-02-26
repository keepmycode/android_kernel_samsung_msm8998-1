/*
 * PGD allocation/freeing
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/slab.h>

#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/tlbflush.h>

#include "mm.h"

#ifdef CONFIG_TIMA_RKP
#include <linux/rkp_entry.h>
#include <linux/spinlock.h>
extern u8 rkp_started;
#endif /* CONFIG_TIMA_RKP */
static struct kmem_cache *pgd_cache;

#ifndef CONFIG_TIMA_RKP
pgd_t *pgd_alloc(struct mm_struct *mm)
{
	if (PGD_SIZE == PAGE_SIZE)
		return (pgd_t *)__get_free_page(PGALLOC_GFP);
	else
		return kmem_cache_alloc(pgd_cache, PGALLOC_GFP);
}
#else
pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret = NULL;
	ret = (pgd_t *) rkp_ro_alloc();
	if (!ret) {
		if (PGD_SIZE == PAGE_SIZE)
			ret = (pgd_t *)__get_free_page(PGALLOC_GFP);
		else
			ret = kmem_cache_alloc(pgd_cache, PGALLOC_GFP);
	}
	if (rkp_started)
		rkp_call(RKP_PGD_NEW, (unsigned long)ret, 0, 0, 0, 0);
	return ret;
}
#endif

#ifndef  CONFIG_TIMA_RKP
void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	if (PGD_SIZE == PAGE_SIZE)
		free_page((unsigned long)pgd);
	else
		kmem_cache_free(pgd_cache, pgd);
}
#else
void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	rkp_call(RKP_PGD_FREE, (unsigned long)pgd, 0, 0, 0, 0);
	/* if pgd memory come from read only buffer, the put it back */
	if(is_rkp_ro_page((unsigned long)pgd))
		rkp_ro_free((void*)pgd);
	else {
		if (PGD_SIZE == PAGE_SIZE)
			free_page((unsigned long)pgd);
		else
			kmem_cache_free(pgd_cache, pgd);
	}
}
#endif

void __init pgd_cache_init(void)
{
	if (PGD_SIZE == PAGE_SIZE)
		return;

	/*
	 * Naturally aligned pgds required by the architecture.
	 */
	pgd_cache = kmem_cache_create("pgd_cache", PGD_SIZE, PGD_SIZE,
				      SLAB_PANIC, NULL);
}
