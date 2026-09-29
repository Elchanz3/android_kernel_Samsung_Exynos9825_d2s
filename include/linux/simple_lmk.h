/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019-2020 Sultan Alsawaf <sultan@kerneltoast.com>.
 */
#ifndef _SIMPLE_LMK_H_
#define _SIMPLE_LMK_H_

#include <linux/types.h>

struct mm_struct;

#ifdef CONFIG_ANDROID_SIMPLE_LMK
bool simple_lmk_enabled(void);
void simple_lmk_mm_freed(struct mm_struct *mm);
#else
static inline bool simple_lmk_enabled(void)
{
	return false;
}

static inline void simple_lmk_mm_freed(struct mm_struct *mm)
{
}
#endif

#endif /* _SIMPLE_LMK_H_ */
