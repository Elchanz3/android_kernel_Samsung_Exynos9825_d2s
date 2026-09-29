/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOC_SAMSUNG_EXYNOS_SOC_INTERFACE_H__
#define __SOC_SAMSUNG_EXYNOS_SOC_INTERFACE_H__

#include <linux/types.h>

/* Numeric tables from the Exynos 9825 ECT dump. Data is owned by the .c file. */
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

struct exynos_soc_gpu_policy {
	u32 min_threshold;
	u32 max_threshold;
	u32 down_staycount;
	u32 mem_freq;
	u32 cpu_little_min_freq;
	u32 cpu_middle_min_freq;
	u32 cpu_big_max_freq;
	u32 llc_ways;
};

/* Layout of the 15 FVMap domains captured from Exynos 9825 ACPM SRAM. */
struct exynos_soc_fvmap_layout {
	u8 levels;
	u8 members;
	u8 plls;
	u16 old_members;
	u16 old_params;
	u16 old_rates;
	u16 new_members;
	u16 new_params;
	u16 new_rates;
	u16 old_pll[3];
	u16 new_pll[3];
	u16 pll_slots[3];
};

enum exynos_soc_consumer {
	EXYNOS_SOC_CONSUMER_VCLK,
	EXYNOS_SOC_CONSUMER_PLL,
	EXYNOS_SOC_CONSUMER_FVMAP,
	EXYNOS_SOC_CONSUMER_CPUFREQ,
	EXYNOS_SOC_CONSUMER_DEVFREQ,
	EXYNOS_SOC_CONSUMER_GPU,
	EXYNOS_SOC_CONSUMER_TMU,
	EXYNOS_SOC_CONSUMER_ISP_COOLING,
	EXYNOS_SOC_CONSUMER_COUNT,
};

int exynos_soc_interface_early_init(void);
void exynos_soc_note_consumer(enum exynos_soc_consumer consumer);
void exynos_soc_note_fvmap_domain(const char *name);
const struct exynos_soc_gpu_policy *exynos_soc_gpu_policy_get(unsigned int level);
/* Runtime GPU tables start at this zero-based catalog index. */
unsigned int exynos_soc_gpu_first_index(void);
unsigned int exynos_soc_gpu_level_count(void);
const struct exynos_soc_fvmap_layout *exynos_soc_fvmap_layout_get(unsigned int domain);
unsigned int exynos_soc_fvmap_layout_count(void);

unsigned int exynos_soc_catalog_count(void);
const struct exynos_soc_catalog_table *exynos_soc_catalog_get(unsigned int index);
const struct exynos_soc_catalog_table *exynos_soc_catalog_find(
		const char *block, const char *name, const char *subname,
		enum exynos_soc_catalog_kind kind);

/* Returns OPPs with matching DVFS/ASV labels; DVS_CP has zero voltage. */
int exynos_soc_get_opp(const char *name, unsigned int asv_version,
		      unsigned int asv_group, unsigned int level,
		      unsigned int *freq_khz, unsigned int *volt_uv);
int exynos_soc_get_limits(const char *name, unsigned int asv_version,
			 unsigned int *min_khz, unsigned int *max_khz,
			 unsigned int *boot_khz, unsigned int *resume_khz);

#endif
