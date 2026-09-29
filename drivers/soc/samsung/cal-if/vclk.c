#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <soc/samsung/exynos-soc_interface.h>

#include "cmucal.h"
#include "vclk.h"
#include "ra.h"
#include "acpm_dvfs.h"
#include "asv.h"

#define ECT_DUMMY_SFR	(0xFFFFFFFF)
unsigned int asv_table_ver = 0;
unsigned int main_rev;
unsigned int sub_rev;

static struct vclk_lut *get_lut(struct vclk *vclk, unsigned int rate)
{
	int i;

	for (i = 0; i < vclk->num_rates; i++)
		if (rate >= vclk->lut[i].rate)
			break;

	if (i == vclk->num_rates)
		return NULL;

	return &vclk->lut[i];
}

static unsigned int get_max_rate(unsigned int from, unsigned int to)
{
	unsigned int max_rate;

	if (from)
		max_rate = (from > to) ? from : to;
	else
		max_rate = to;

	return max_rate;
}

static void __select_switch_pll(struct vclk *vclk,
				unsigned int rate,
				unsigned int select)
{
	if (vclk->ops && vclk->ops->switch_pre)
		vclk->ops->switch_pre(vclk->vrate, rate);

	if (vclk->ops && vclk->ops->switch_trans && select)
		vclk->ops->switch_trans(vclk->vrate, rate);
	else if (vclk->ops && vclk->ops->restore_trans && !select)
		vclk->ops->restore_trans(vclk->vrate, rate);
	else
		ra_select_switch_pll(vclk->switch_info, select);

	if (vclk->ops && vclk->ops->switch_post)
		vclk->ops->switch_post(vclk->vrate, rate);
}

static int transition_switch(struct vclk *vclk, struct vclk_lut *lut,
			     unsigned int switch_rate)
{
	unsigned int *list = vclk->list;
	unsigned int num_list = vclk->num_list;

	/* Change to swithing PLL */
	if (vclk->ops && vclk->ops->trans_pre)
		vclk->ops->trans_pre(vclk->vrate, lut->rate);

	ra_set_clk_by_type(list, lut, num_list, DIV_TYPE, TRANS_HIGH);

	__select_switch_pll(vclk, switch_rate, 1);

	ra_set_clk_by_type(list, lut, num_list, MUX_TYPE, TRANS_FORCE);
	ra_set_clk_by_type(list, lut, num_list, DIV_TYPE, TRANS_LOW);

	vclk->vrate = switch_rate;

	return 0;
}

static int transition_restore(struct vclk *vclk, struct vclk_lut *lut)
{
	unsigned int *list = vclk->list;
	unsigned int num_list = vclk->num_list;

	/* PLL setting */
	ra_set_pll_ops(list, lut, num_list, vclk->ops);

	ra_set_clk_by_type(list, lut, num_list, DIV_TYPE, TRANS_HIGH);

	__select_switch_pll(vclk, lut->rate, 0);

	ra_set_clk_by_type(list, lut, num_list, MUX_TYPE, TRANS_FORCE);
	ra_set_clk_by_type(list, lut, num_list, DIV_TYPE, TRANS_LOW);
	if (vclk->ops && vclk->ops->trans_post)
		vclk->ops->trans_post(vclk->vrate, lut->rate);

	return 0;
}

static int transition(struct vclk *vclk,
		      struct vclk_lut *lut)
{
	unsigned int *list = vclk->list;
	unsigned int num_list = vclk->num_list;

	ra_set_clk_by_type(list, lut, num_list, DIV_TYPE, TRANS_HIGH);
	ra_set_clk_by_type(list, lut, num_list, PLL_TYPE, TRANS_LOW);
	ra_set_clk_by_type(list, lut, num_list, MUX_TYPE, TRANS_FORCE);
	ra_set_clk_by_type(list, lut, num_list, PLL_TYPE, TRANS_HIGH);
	ra_set_clk_by_type(list, lut, num_list, DIV_TYPE, TRANS_LOW);

	return 0;
}

static bool is_switching_pll_ops(struct vclk *vclk, int cmd)
{
	int i;

	if (!vclk->switch_info)
		return false;

	if (cmd != ONESHOT_TRANS)
		return true;

	for (i = 0; i < vclk->num_list; i++) {
		if (IS_PLL(vclk->list[i]))
			return true;
	}

	return false;
}

static int __vclk_set_rate(unsigned int id, unsigned int rate, int cmd)
{
	struct vclk *vclk;
	struct vclk_lut *new_lut, *switch_lut;
	unsigned int switch_rate, max_rate;

	if (!IS_VCLK(id))
		return ra_set_rate(id, rate);

	vclk = cmucal_get_node(id);
	if (!vclk || !vclk->lut)
		return -EVCLKINVAL;

	if (IS_DFS_VCLK(id) || IS_COMMON_VCLK(id))
		new_lut = get_lut(vclk, rate);
	else
		new_lut = get_lut(vclk, rate / 1000);

	if (!new_lut)
		return -EVCLKINVAL;

	if (is_switching_pll_ops(vclk, cmd)) {
		switch_lut = new_lut;
		switch_rate = rate;
		if (is_oneshot_trans(cmd)) {
			max_rate = get_max_rate(vclk->vrate, rate);
			switch_rate = ra_set_rate_switch(vclk->switch_info,
							 max_rate);
			switch_lut = get_lut(vclk, switch_rate);
			if (!switch_lut)
				return -EVCLKINVAL;
		}
		if (is_switch_trans(cmd))
			transition_switch(vclk, switch_lut, switch_rate);
		if (is_restore_trans(cmd))
			transition_restore(vclk, new_lut);
	} else if (vclk->seq) {
		ra_set_clk_by_seq(vclk->list,
				  new_lut,
				  vclk->seq,
				  vclk->num_list);
	} else {
		transition(vclk, new_lut);
	}

	vclk->vrate = rate;

	return 0;
}

int vclk_set_rate(unsigned int id, unsigned long rate)
{
	int ret;

	ret = __vclk_set_rate(id, rate, ONESHOT_TRANS);

	return ret;
}

int vclk_set_rate_switch(unsigned int id, unsigned long rate)
{
	int ret;

	ret = __vclk_set_rate(id, rate, SWITCH_TRANS);

	return ret;
}

int vclk_set_rate_restore(unsigned int id, unsigned long rate)
{
	int ret;

	ret = __vclk_set_rate(id, rate, RESTORE_TRANS);

	return ret;
}

unsigned long vclk_recalc_rate(unsigned int id)
{
	struct vclk *vclk;
	int i, ret;

	if (!IS_VCLK(id))
		return ra_recalc_rate(id);

	vclk = cmucal_get_node(id);
	if (!vclk)
		return 0;

	if (IS_DFS_VCLK(vclk->id) ||
	    IS_COMMON_VCLK(vclk->id) ||
	    IS_ACPM_VCLK(vclk->id)) {
		for (i = 0; i < vclk->num_rates; i++) {
			ret = ra_compare_clk_list(vclk->lut[i].params,
						  vclk->list,
						  vclk->num_list);
			if (!ret) {
				vclk->vrate = vclk->lut[i].rate;
				break;
			}
		}

		if (i == vclk->num_rates) {
			vclk->vrate = 0;
			pr_debug("%s:%x failed\n", __func__, id);
		}
	} else {
		vclk->vrate = ra_recalc_rate(vclk->list[0]);
	}

	return vclk->vrate;
}

unsigned long vclk_get_rate(unsigned int id)
{
	struct vclk *vclk;

	if (IS_VCLK(id)) {
		vclk = cmucal_get_node(id);
		if (vclk)
			return vclk->vrate;
	}

	return 0;
}

int vclk_set_enable(unsigned int id)
{
	struct vclk *vclk;
	int ret = -EVCLKINVAL;

	if (IS_GATE_VCLK(id)) {
		vclk = cmucal_get_node(id);
		if (vclk)
			ret = ra_set_list_enable(vclk->list, vclk->num_list);
	} else if (IS_VCLK(id)){
		ret = 0;
	} else {
		ret = ra_set_enable(id, 1);
	}

	return ret;
}

int vclk_set_disable(unsigned int id)
{
	struct vclk *vclk;
	int ret = -EVCLKINVAL;

	if (IS_GATE_VCLK(id)) {
		vclk = cmucal_get_node(id);
		if (vclk)
			ret = ra_set_list_disable(vclk->list, vclk->num_list);
	} else if (IS_VCLK(id)){
		ret = 0;
	} else {
		ret = ra_set_enable(id, 0);
	}

	return ret;
}

unsigned int vclk_get_lv_num(unsigned int id)
{
	struct vclk *vclk;
	int lv_num = 0;

	vclk = cmucal_get_node(id);

	if (vclk && vclk->lut)
		lv_num = vclk->num_rates;

	return lv_num;

}

unsigned int vclk_get_max_freq(unsigned int id)
{
	struct vclk *vclk;
	int rate = 0;

	vclk = cmucal_get_node(id);

	if (vclk && vclk->lut)
		rate = vclk->max_freq;

	return rate;
}

unsigned int vclk_get_min_freq(unsigned int id)
{
	struct vclk *vclk;
	int rate = 0;

	vclk = cmucal_get_node(id);

	if (vclk && vclk->lut)
		rate = vclk->min_freq;

	return rate;
}

int vclk_get_rate_table(unsigned int id, unsigned long *table)
{
	struct vclk *vclk;
	int i;
	unsigned int nums = 0;

	vclk = cmucal_get_node(id);
	if (!vclk || !IS_VCLK(vclk->id))
		return 0;
	if (vclk->lut) {
		for (i = 0; i < vclk->num_rates; i++)
			table[i] = vclk->lut[i].rate;
		nums = vclk->num_rates;
	}

	return nums;
}

int vclk_get_bigturbo_table(unsigned int *table)
{
	/* BIGTURBO is absent from the supplied Exynos 9825 catalog. */
	return -EVCLKNOENT;
}

unsigned int vclk_get_boot_freq(unsigned int id)
{
	struct vclk *vclk;
	unsigned int rate = 0;

	vclk = cmucal_get_node(id);
	if (!vclk || !(IS_DFS_VCLK(vclk->id) || IS_ACPM_VCLK(vclk->id)))
		return rate;

	if (vclk->boot_freq)
		rate = vclk->boot_freq;
	else
		rate = (unsigned int)vclk_recalc_rate(id);

	return rate;
}

unsigned int vclk_get_resume_freq(unsigned int id)
{
	struct vclk *vclk;
	unsigned int rate = 0;

	vclk = cmucal_get_node(id);
	if (!vclk || !(IS_DFS_VCLK(vclk->id) || IS_ACPM_VCLK(vclk->id)))
		return rate;

	if (vclk->resume_freq)
		rate = vclk->resume_freq;
	else
		rate = (unsigned int)vclk_recalc_rate(id);

	return rate;
}

static int vclk_get_dfs_info(struct vclk *vclk)
{
	const struct exynos_soc_catalog_table *levels, *params;
	unsigned int min_khz, max_khz, boot_khz, resume_khz;
	int i, j, ret;

	levels = exynos_soc_catalog_find("DVFS", vclk->name, "levels",
					 EXYNOS_SOC_DVFS_LEVELS);
	params = exynos_soc_catalog_find("DVFS", vclk->name, "params",
					 EXYNOS_SOC_DVFS_PARAMS);
	if (!levels || !params)
		return -EVCLKNOENT;
	if (levels->rows != 1 || !levels->cols ||
	    levels->cols != params->rows || !params->cols)
		return -EVCLKINVAL;

	ret = exynos_soc_get_limits(vclk->name, asv_table_ver,
				    &min_khz, &max_khz, &boot_khz, &resume_khz);
	if (ret)
		return -EVCLKINVAL;

	vclk->num_rates = levels->cols;
	vclk->num_list = params->cols;
	vclk->min_freq = min_khz;
	vclk->max_freq = max_khz;
	vclk->boot_freq = boot_khz;
	vclk->resume_freq = resume_khz;
	vclk->list = kcalloc(vclk->num_list, sizeof(*vclk->list), GFP_KERNEL);
	if (!vclk->list)
		return -EVCLKNOMEM;
	vclk->lut = kcalloc(vclk->num_rates, sizeof(*vclk->lut), GFP_KERNEL);
	if (!vclk->lut) {
		ret = -EVCLKNOMEM;
		goto err_list;
	}

	for (i = 0; i < vclk->num_rates; i++) {
		int *row;

		if (levels->data[i] > U32_MAX) {
			ret = -EVCLKINVAL;
			goto err_lut;
		}
		row = kcalloc(vclk->num_list, sizeof(*row), GFP_KERNEL);
		if (!row) {
			ret = -EVCLKNOMEM;
			goto err_lut;
		}
		vclk->lut[i].rate = levels->data[i];
		vclk->lut[i].params = row;
		for (j = 0; j < vclk->num_list; j++) {
			u64 value = params->data[i * vclk->num_list + j];

			if (value > INT_MAX) {
				ret = -EVCLKINVAL;
				goto err_lut;
			}
			row[j] = value;
		}
	}
	exynos_soc_note_consumer(EXYNOS_SOC_CONSUMER_VCLK);
	return 0;

err_lut:
	for (i = 0; i < vclk->num_rates; i++)
		kfree(vclk->lut[i].params);
	kfree(vclk->lut);
	vclk->lut = NULL;
err_list:
	kfree(vclk->list);
	vclk->list = NULL;
	return ret;
}

static void vclk_bind(void)
{
	struct vclk *vclk;
	int i, ret;

	for (i = 0; i < cmucal_get_list_size(ACPM_VCLK_TYPE); i++) {
		vclk = cmucal_get_node(ACPM_VCLK_TYPE | i);
		if (!vclk)
			continue;
		ret = vclk_get_dfs_info(vclk);
		if (ret)
			pr_err("SoC interface DVFS [%s] unavailable: %d\n",
				vclk->name, ret);
	}
}

int vclk_register_ops(unsigned int id, struct vclk_trans_ops *ops)
{
	struct vclk *vclk;

	if (IS_DFS_VCLK(id)) {
		vclk = cmucal_get_node(id);
		if (!vclk)
			return -EVCLKINVAL;
		vclk->ops = ops;

		return 0;
	}

	return -EVCLKNOENT;
}

int __init vclk_initialize(void)
{
	pr_info("vclk initialize for cmucal\n");

	ra_init();

	asv_table_ver = asv_table_init();
	id_get_rev(&main_rev, &sub_rev);

	vclk_bind();

	return 0;
}
