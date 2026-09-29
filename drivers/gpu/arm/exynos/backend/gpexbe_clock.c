/* SPDX-License-Identifier: GPL-2.0 */

/*
 * (C) COPYRIGHT 2021 Samsung Electronics Inc. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 */

#include <soc/samsung/cal-if.h>
#include <soc/samsung/exynos-soc_interface.h>

#include <gpexbe_devicetree.h>

#include <gpexbe_clock.h>
#include <gpex_utils.h>
#include <gpex_debug.h>

struct _clock_backend_info {
	int boot_clock;
	int max_clock_limit;
};

static struct _clock_backend_info pm_info;
static unsigned int cal_id;

int gpexbe_clock_get_level_num(void)
{
	const struct exynos_soc_catalog_table *levels;

	levels = exynos_soc_catalog_find("DVFS", "dvfs_g3d", "levels",
					 EXYNOS_SOC_DVFS_LEVELS);
	return levels ? levels->cols : 0;
}

int gpexbe_clock_get_rate_asv_table(struct freq_volt *fv_array, int level_num)
{
	unsigned int freq, volt;
	int group, i, ret;

	if (!fv_array || level_num != gpexbe_clock_get_level_num())
		return -EINVAL;
	group = cal_asv_get_grp(cal_id);
	if (group < 0)
		return -EINVAL;
	for (i = 0; i < level_num; i++) {
		ret = exynos_soc_get_opp("dvfs_g3d", cal_asv_get_tablever(),
					 group, i, &freq, &volt);
		if (ret)
			return ret;
		fv_array[i].freq = freq;
		fv_array[i].volt = volt;
	}
	return level_num;
}

int gpexbe_clock_get_boot_freq()
{
	return pm_info.boot_clock;
}

int gpexbe_clock_get_max_freq()
{
	return pm_info.max_clock_limit;
}

int gpexbe_clock_set_rate(int clk)
{
	int ret = 0;

	gpex_debug_new_record(HIST_CLOCK);
	gpex_debug_record_prev_data(HIST_CLOCK, gpexbe_clock_get_rate());

	ret = cal_dfs_set_rate(cal_id, clk);

	gpex_debug_record_time(HIST_CLOCK);
	gpex_debug_record_code(HIST_CLOCK, ret);
	gpex_debug_record_new_data(HIST_CLOCK, clk);

	if (ret)
		gpex_debug_incr_error_cnt(HIST_CLOCK);

	return ret;
}

int gpexbe_clock_get_rate()
{
	return cal_dfs_get_rate(cal_id);
}

int gpexbe_clock_init()
{
	const struct exynos_soc_catalog_table *levels;
	unsigned int min_khz, max_khz, boot_khz, resume_khz;
	int i, ret;

	cal_id = gpexbe_devicetree_get_int(g3d_cmu_cal_id);

	if (!cal_id) {
		/* TODO: print error cal id not found */
		return -1;
	}

	levels = exynos_soc_catalog_find("DVFS", "dvfs_g3d", "levels",
					 EXYNOS_SOC_DVFS_LEVELS);
	ret = exynos_soc_get_limits("dvfs_g3d", cal_asv_get_tablever(),
				    &min_khz, &max_khz, &boot_khz, &resume_khz);
	if (!levels || ret)
		return -EINVAL;
	pm_info.boot_clock = boot_khz;
	pm_info.max_clock_limit = 0;
	for (i = 0; i < levels->cols; i++) {
		if (levels->data[i] <= max_khz) {
			pm_info.max_clock_limit = levels->data[i];
			break;
		}
	}
	if (!pm_info.max_clock_limit)
		return -EINVAL;

	gpex_utils_get_exynos_context()->pm_info = &pm_info;

	return 0;
}

void gpexbe_clock_term()
{
	cal_id = 0;
	pm_info.boot_clock = 0;
	pm_info.max_clock_limit = 0;
}
