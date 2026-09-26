/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOC_SAMSUNG_EXYNOS_SOC_INTERFACE_H__
#define __SOC_SAMSUNG_EXYNOS_SOC_INTERFACE_H__

#include <linux/types.h>

enum exynos_soc_catalog_kind {
	EXYNOS_SOC_PLL_RATES,
	EXYNOS_SOC_DVFS_LEVELS,
	EXYNOS_SOC_DVFS_SFR,
	EXYNOS_SOC_DVFS_PARAMS,
	EXYNOS_SOC_ASV_FREQS,
	EXYNOS_SOC_ASV_TABLE,
	EXYNOS_SOC_GEN_TABLE,
	EXYNOS_SOC_NEWTIME_TABLE,
	EXYNOS_SOC_MINLOCK_TABLE,
	EXYNOS_SOC_PIDTM_TEMPS,
	EXYNOS_SOC_PIDTM_PARAMS,
	EXYNOS_SOC_THERMAL_RANGES,
};

struct exynos_soc_catalog_table {
	const char *block;
	const char *name;
	const char *subname;
	enum exynos_soc_catalog_kind kind;
	unsigned int rows;
	unsigned int cols;
	unsigned int aux0;
	unsigned int aux1;
	const u64 *data;
};

unsigned int exynos_soc_catalog_count(void);
const struct exynos_soc_catalog_table *exynos_soc_catalog_get(unsigned int index);
const struct exynos_soc_catalog_table *exynos_soc_catalog_find(
		const char *block, const char *name, const char *subname,
		enum exynos_soc_catalog_kind kind);

/* Returns only OPPs with matching DVFS and ASV labels and nonzero voltage. */
int exynos_soc_get_opp(const char *name, unsigned int asv_version,
		      unsigned int asv_group, unsigned int level,
		      unsigned int *freq_khz, unsigned int *volt_uv);

#endif
