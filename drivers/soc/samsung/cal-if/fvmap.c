#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/kobject.h>
#include <linux/seq_file.h>
#include <soc/samsung/cal-if.h>
#include <soc/samsung/exynos-soc_interface.h>

#include "fvmap.h"
#include "cmucal.h"
#include "vclk.h"
#include "ra.h"

#define FVMAP_SIZE		(SZ_8K)
#define STEP_UV			(6250)
#define SOC_FVMAP_MAX_DOMAINS	(32)
#define SOC_FVMAP_MAX_LEVELS	(64)
#define FVMAP_DUMP_MAX_SIZE	(SZ_256K)
#define SOC_FVMAP_PAYLOAD_END	(0x09f0)
#define SOC_FVMAP_G3D_DOMAIN	(10)

void __iomem *fvmap_base;
void __iomem *sram_fvmap_base;
static bool soc_fvmap_ready[SOC_FVMAP_MAX_DOMAINS];

static ssize_t fvmap_sram_raw_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	if (off < 0)
		return -EINVAL;
	if (off >= FVMAP_SIZE)
		return 0;
	if (!sram_fvmap_base)
		return -ENODEV;

	count = min_t(size_t, count, FVMAP_SIZE - off);
	memcpy_fromio(buf, (u8 __iomem *)sram_fvmap_base + off, count);
	return count;
}

static struct bin_attribute fvmap_sram_raw_attr =
	__BIN_ATTR(sram_raw, 0444, fvmap_sram_raw_read, NULL, FVMAP_SIZE);

static int init_margin_table[MAX_MARGIN_ID];
static int volt_offset_percent = 0;
static int percent_margin_table[MAX_MARGIN_ID];

static bool fvmap_dump_region_valid(unsigned int offset, size_t bytes)
{
	return offset <= FVMAP_SIZE && bytes <= FVMAP_SIZE - offset;
}

static bool fvmap_payload_region_valid(unsigned int offset, size_t bytes)
{
	return offset <= SOC_FVMAP_PAYLOAD_END &&
	       bytes <= SOC_FVMAP_PAYLOAD_END - offset;
}

static void fvmap_format_sram_dump(struct seq_file *seq, const u8 *snapshot)
{
	const struct fvmap_header *headers = (const void *)snapshot;
	int domains = cmucal_get_list_size(ACPM_VCLK_TYPE);
	int i, j, k;

	seq_puts(seq, "SRAM Frequency-Voltage Table Data (Exynos 9825)\n");
	seq_puts(seq, "================================================\n");
	seq_printf(seq, "FVMap region: %u bytes; source: live ACPM SRAM\n",
		   FVMAP_SIZE);
	seq_puts(seq, "Values below are from SRAM, including domains rejected by the interface.\n");

	if (domains < 0 || domains > FVMAP_SIZE / sizeof(*headers)) {
		seq_printf(seq, "Invalid domain count: %d\n", domains);
		return;
	}

	for (i = 0; i < domains; i++) {
		const struct fvmap_header *h = &headers[i];
		const struct rate_volt *opps;
		const u16 *members;
		const u8 *params;
		struct vclk *vclk = cmucal_get_node(ACPM_VCLK_TYPE | i);
		const char *name = vclk ? vclk->name : "unknown";
		unsigned int margin_id = vclk ? vclk->margin_id : MAX_MARGIN_ID;
		size_t param_bytes = (size_t)h->num_of_lv * h->num_of_members;

		seq_printf(seq, "\nDomain: %s (ID: 0x%x, Margin ID: %u)\n",
			   name, i, margin_id);
		seq_printf(seq, "Levels: %u, Members: %u, PLLs: %u, MUXes: %u, DIVs: %u, Gates: %u\n",
			   h->num_of_lv, h->num_of_members, h->num_of_pll,
			   h->num_of_mux, h->num_of_div, h->num_of_gate);
		seq_printf(seq, "Header: type=0x%02x, init_level=%u, gear_ratio=%u\n",
			   h->dvfs_type, h->init_lv, h->gearratio);
		seq_printf(seq, "Offsets: members=0x%04x, ratevolt=0x%04x, tables=0x%04x\n",
			   h->o_members, h->o_ratevolt, h->o_tables);
		seq_printf(seq, "Block addresses: 0x%04x 0x%04x 0x%04x\n",
			   h->block_addr[0], h->block_addr[1], h->block_addr[2]);
		if (margin_id < MAX_MARGIN_ID)
			seq_printf(seq, "Margin: boot=%d, runtime=%d%%\n",
				   init_margin_table[margin_id],
				   percent_margin_table[margin_id]);
		seq_printf(seq, "Interface installed: %s\n",
			   i < ARRAY_SIZE(soc_fvmap_ready) &&
			   soc_fvmap_ready[i] ? "yes" : "no");

		if (!h->num_of_lv || h->num_of_lv > SOC_FVMAP_MAX_LEVELS ||
		    h->num_of_pll > h->num_of_members ||
		    !fvmap_dump_region_valid(h->o_ratevolt,
					   h->num_of_lv * sizeof(*opps)) ||
		    !fvmap_dump_region_valid(h->o_members,
					   h->num_of_members * sizeof(*members)) ||
		    !fvmap_dump_region_valid(h->o_tables, param_bytes)) {
			seq_puts(seq, "Invalid table shape or offsets; domain data skipped.\n");
			continue;
		}

		opps = (const void *)(snapshot + h->o_ratevolt);
		members = (const void *)(snapshot + h->o_members);
		params = snapshot + h->o_tables;

		seq_puts(seq, "----------------------------------------------\n");
		seq_puts(seq, "Level | Frequency(kHz) | Voltage(uV) | Params\n");
		seq_puts(seq, "----------------------------------------------\n");
		for (j = 0; j < h->num_of_lv; j++) {
			seq_printf(seq, "%5d | %14u | %11u |",
				   j, opps[j].rate, opps[j].volt);
			for (k = 0; k < h->num_of_members; k++)
				seq_printf(seq, " %u", params[j * h->num_of_members + k]);
			seq_putc(seq, '\n');
		}

		seq_puts(seq, "Members (raw SRAM offsets):\n");
		for (j = 0; j < h->num_of_members; j++) {
			seq_printf(seq, "  %2d: 0x%04x%s\n", j, members[j],
				   j < h->num_of_pll ? " (PLL descriptor)" : "");
			if (j < h->num_of_pll) {
				const struct pll_header *pll;
				unsigned int off = members[j];

				if (!fvmap_dump_region_valid(off, sizeof(*pll))) {
					seq_puts(seq, "      Invalid PLL descriptor offset.\n");
					continue;
				}
				pll = (const void *)(snapshot + off);
				seq_printf(seq, "      addr=0x%08x, lock_offset=0x%04x, level_field=%u\n",
					   pll->addr, pll->o_lock, pll->level);
				if (pll->level > SOC_FVMAP_MAX_LEVELS ||
				    !fvmap_dump_region_valid(off + sizeof(*pll),
							   pll->level * sizeof(u32))) {
					seq_puts(seq, "      PMS entries exceed dump bounds.\n");
					continue;
				}
				for (k = 0; k < pll->level; k++)
					seq_printf(seq, "      PMS[%d]=0x%08x\n",
						   k, pll->pms[k]);
			}
		}
	}
}

static ssize_t fvmap_sram_dump_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct seq_file seq = { };
	u8 *snapshot;
	size_t size = PAGE_SIZE;
	int ret;

	if (off < 0)
		return -EINVAL;
	if (!sram_fvmap_base)
		return -ENODEV;

	snapshot = kmalloc(FVMAP_SIZE, GFP_KERNEL);
	if (!snapshot)
		return -ENOMEM;
	memcpy_fromio(snapshot, sram_fvmap_base, FVMAP_SIZE);

	for (;;) {
		seq.buf = kvmalloc(size, GFP_KERNEL);
		if (!seq.buf) {
			ret = -ENOMEM;
			break;
		}
		seq.size = size;
		seq.count = 0;
		fvmap_format_sram_dump(&seq, snapshot);
		if (!seq_has_overflowed(&seq)) {
			if (off >= seq.count)
				ret = 0;
			else {
				ret = min_t(size_t, count, seq.count - off);
				memcpy(buf, seq.buf + off, ret);
			}
			break;
		}
		kvfree(seq.buf);
		seq.buf = NULL;
		if (size >= FVMAP_DUMP_MAX_SIZE) {
			ret = -EOVERFLOW;
			break;
		}
		size *= 2;
	}

	kvfree(seq.buf);
	kfree(snapshot);
	return ret;
}

static struct bin_attribute fvmap_sram_dump_attr =
	__BIN_ATTR(sram_dump, 0444, fvmap_sram_dump_read, NULL, 0);

bool fvmap_is_interface_ready(unsigned int id)
{
	unsigned int idx = GET_IDX(id);

	return IS_ACPM_VCLK(id) && idx < ARRAY_SIZE(soc_fvmap_ready) &&
	       soc_fvmap_ready[idx];
}

static int __init get_mif_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_MIF] = volt;

	return 0;
}
early_param("mif", get_mif_volt);

static int __init get_int_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_INT] = volt;

	return 0;
}
early_param("int", get_int_volt);

static int __init get_big_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_BIG] = volt;

	return 0;
}
early_param("big", get_big_volt);

static int __init get_mid_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_MID] = volt;

	return 0;
}
early_param("mid", get_mid_volt);

static int __init get_lit_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_LIT] = volt;

	return 0;
}
early_param("lit", get_lit_volt);

static int __init get_g3d_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_G3D] = volt;

	return 0;
}
early_param("g3d", get_g3d_volt);

static int __init get_intcam_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_INTCAM] = volt;

	return 0;
}
early_param("intcam", get_intcam_volt);

static int __init get_cam_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_CAM] = volt;

	return 0;
}
early_param("cam", get_cam_volt);

static int __init get_disp_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_DISP] = volt;

	return 0;
}
early_param("disp", get_disp_volt);

static int __init get_g3dm_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_G3DM] = volt;

	return 0;
}
early_param("g3dm", get_g3dm_volt);

static int __init get_cp_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_CP] = volt;

	return 0;
}
early_param("cp", get_cp_volt);

static int __init get_fsys0_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_FSYS0] = volt;

	return 0;
}
early_param("fsys0", get_fsys0_volt);

static int __init get_aud_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_AUD] = volt;

	return 0;
}
early_param("aud", get_aud_volt);

static int __init get_iva_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_IVA] = volt;

	return 0;
}
early_param("iva", get_iva_volt);

static int __init get_score_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_SCORE] = volt;

	return 0;
}
early_param("score", get_score_volt);

static int __init get_npu_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_NPU] = volt;

	return 0;
}
early_param("npu", get_npu_volt);

static int __init get_mfc_volt(char *str)
{
	int volt;

	get_option(&str, &volt);
	init_margin_table[MARGIN_MFC] = volt;

	return 0;
}
early_param("mfc", get_mfc_volt);

static int __init get_percent_margin_volt(char *str)
{
	int percent;

	get_option(&str, &percent);
	volt_offset_percent = percent;

	return 0;
}
early_param("volt_offset_percent", get_percent_margin_volt);

int fvmap_set_raw_voltage_table(unsigned int id, int uV)
{
	struct fvmap_header *fvmap_header;
	struct rate_volt_header *fv_table;
	int num_of_lv;
	int idx, i;

	idx = GET_IDX(id);
	if (!fvmap_is_interface_ready(id))
		return -EINVAL;

	fvmap_header = sram_fvmap_base;
	fv_table = sram_fvmap_base + fvmap_header[idx].o_ratevolt;
	num_of_lv = fvmap_header[idx].num_of_lv;

	for (i = 0; i < num_of_lv; i++)
		fv_table->table[i].volt += uV;

	return 0;
}

int fvmap_get_voltage_table(unsigned int id, unsigned int *table)
{
	struct fvmap_header *fvmap_header = fvmap_base;
	struct rate_volt_header *fv_table;
	int idx, i;
	int num_of_lv;

	if (!IS_ACPM_VCLK(id))
		return 0;

	idx = GET_IDX(id);
	if (!fvmap_is_interface_ready(id))
		return 0;

	fvmap_header = fvmap_base;
	fv_table = fvmap_base + fvmap_header[idx].o_ratevolt;
	num_of_lv = fvmap_header[idx].num_of_lv;

	for (i = 0; i < num_of_lv; i++)
		table[i] = fv_table->table[i].volt;

	return num_of_lv;

}

int fvmap_get_raw_voltage_table(unsigned int id)
{
	struct fvmap_header *fvmap_header;
	struct rate_volt_header *fv_table;
	int idx, i;
	int num_of_lv;

	idx = GET_IDX(id);
	if (!fvmap_is_interface_ready(id))
		return -EINVAL;

	fvmap_header = sram_fvmap_base;
	fv_table = sram_fvmap_base + fvmap_header[idx].o_ratevolt;
	num_of_lv = fvmap_header[idx].num_of_lv;

	for (i = 0; i < num_of_lv; i++)
		printk("dvfs id : %d  %d Khz : %d uv\n",
		       ACPM_VCLK_TYPE | id, fv_table->table[i].rate,
		       fv_table->table[i].volt);

	return 0;
}

static void check_percent_margin(struct rate_volt_header *head, unsigned int num_of_lv)
{
	int org_volt;
	int percent_volt;
	int i;

	if (!volt_offset_percent)
		return;

	for (i = 0; i < num_of_lv; i++) {
		org_volt = head->table[i].volt;
		percent_volt = org_volt * volt_offset_percent / 100;
		head->table[i].volt = org_volt + rounddown(percent_volt, STEP_UV);
	}
}

static int get_vclk_id_from_margin_id(int margin_id)
{
	int size = cmucal_get_list_size(ACPM_VCLK_TYPE);
	int i;
	struct vclk *vclk;

	for (i = 0; i < size; i++) {
		vclk = cmucal_get_node(ACPM_VCLK_TYPE | i);

		if (vclk->margin_id == margin_id)
			return i;
	}

	return -EINVAL;
}

#define attr_percent(margin_id, type)								\
static ssize_t show_##type##_percent								\
(struct kobject *kobj, struct kobj_attribute *attr, char *buf)					\
{												\
	return snprintf(buf, PAGE_SIZE, "%d\n", percent_margin_table[margin_id]);		\
}												\
												\
static ssize_t store_##type##_percent								\
(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)		\
{												\
	int input, vclk_id;									\
												\
	if (!sscanf(buf, "%d", &input))								\
		return -EINVAL;									\
												\
	if (input < -100 || input > 100)							\
		return -EINVAL;									\
												\
	vclk_id = get_vclk_id_from_margin_id(margin_id);					\
	if (vclk_id == -EINVAL)									\
		return vclk_id;									\
	percent_margin_table[margin_id] = input;						\
	cal_dfs_set_volt_margin(vclk_id | ACPM_VCLK_TYPE, input);				\
												\
	return count;										\
}												\
												\
static struct kobj_attribute type##_percent =							\
__ATTR(type##_percent, 0600,									\
	show_##type##_percent, store_##type##_percent)

attr_percent(MARGIN_MIF, mif_margin);
attr_percent(MARGIN_INT, int_margin);
attr_percent(MARGIN_BIG, big_margin);
attr_percent(MARGIN_MID, mid_margin);
attr_percent(MARGIN_LIT, lit_margin);
attr_percent(MARGIN_G3D, g3d_margin);
attr_percent(MARGIN_INTCAM, intcam_margin);
attr_percent(MARGIN_CAM, cam_margin);
attr_percent(MARGIN_DISP, disp_margin);
attr_percent(MARGIN_CP, cp_margin);
attr_percent(MARGIN_FSYS0, fsys0_margin);
attr_percent(MARGIN_AUD, aud_margin);
attr_percent(MARGIN_IVA, iva_margin);
attr_percent(MARGIN_SCORE, score_margin);
attr_percent(MARGIN_NPU, npu_margin);
attr_percent(MARGIN_MFC, mfc_margin);

static struct attribute *percent_margin_attrs[] = {
	&mif_margin_percent.attr,
	&int_margin_percent.attr,
	&big_margin_percent.attr,
	&mid_margin_percent.attr,
	&lit_margin_percent.attr,
	&g3d_margin_percent.attr,
	&intcam_margin_percent.attr,
	&cam_margin_percent.attr,
	&disp_margin_percent.attr,
	&cp_margin_percent.attr,
	&fsys0_margin_percent.attr,
	&aud_margin_percent.attr,
	&iva_margin_percent.attr,
	&score_margin_percent.attr,
	&npu_margin_percent.attr,
	&mfc_margin_percent.attr,
	NULL,
};

static const struct attribute_group percent_margin_group = {
	.attrs = percent_margin_attrs,
};

/* Rebuild the captured 15-domain SRAM layout inside its original 0x9f0 bytes. */
static int fvmap_install_interface_layout(void __iomem *sram_base)
{
	const struct exynos_soc_fvmap_layout *layout;
	const struct exynos_soc_catalog_table *params, *pll_rates;
	struct fvmap_header *source, *target;
	struct rate_volt *g3d_rates;
	struct vclk *g3d;
	u8 *old, *image;
	unsigned int freq, volt, i, j, offset, bytes;
	unsigned int version = cal_asv_get_tablever();
	int group, ret = -EINVAL;
	u32 pms;
	u16 member;

	if (cmucal_get_list_size(ACPM_VCLK_TYPE) !=
	    exynos_soc_fvmap_layout_count())
		return -EINVAL;
	old = kmalloc(FVMAP_SIZE, GFP_KERNEL);
	image = kmalloc(FVMAP_SIZE, GFP_KERNEL);
	if (!old || !image) {
		ret = -ENOMEM;
		goto out;
	}
	memcpy_fromio(old, sram_base, FVMAP_SIZE);
	memcpy(image, old, FVMAP_SIZE);
	source = (struct fvmap_header *)old;
	target = (struct fvmap_header *)image;

	for (i = 0; i < exynos_soc_fvmap_layout_count(); i++) {
		layout = exynos_soc_fvmap_layout_get(i);
		if (!layout || source[i].num_of_lv != layout->levels ||
		    source[i].num_of_members != layout->members ||
		    source[i].num_of_pll != layout->plls ||
		    source[i].o_members != layout->old_members ||
		    source[i].o_tables != layout->old_params ||
		    source[i].o_ratevolt != layout->old_rates ||
		    !fvmap_payload_region_valid(layout->old_members,
					2 * layout->members) ||
		    !fvmap_payload_region_valid(layout->new_members,
					2 * layout->members) ||
		    !fvmap_payload_region_valid(layout->old_params,
					layout->levels * layout->members) ||
		    !fvmap_payload_region_valid(layout->new_params,
					(layout->levels + (i == SOC_FVMAP_G3D_DOMAIN)) *
					layout->members) ||
		    !fvmap_payload_region_valid(layout->old_rates,
					8 * layout->levels) ||
		    !fvmap_payload_region_valid(layout->new_rates,
					8 * (layout->levels +
					     (i == SOC_FVMAP_G3D_DOMAIN))))
			goto out;

		memcpy(image + layout->new_members,
		       old + layout->old_members, 2 * layout->members);
		memcpy(image + layout->new_params,
		       old + layout->old_params,
		       layout->levels * layout->members);
		memcpy(image + layout->new_rates,
		       old + layout->old_rates, 8 * layout->levels);
		for (j = 0; j < layout->plls; j++) {
			memcpy(&member, old + layout->old_members + 2 * j,
			       sizeof(member));
			offset = layout->old_pll[j];
			bytes = sizeof(struct pll_header) +
				4 * layout->pll_slots[j];
			if (member != offset ||
			    !fvmap_payload_region_valid(offset, bytes) ||
			    !fvmap_payload_region_valid(layout->new_pll[j], bytes) ||
			    ((struct pll_header *)(old + offset))->level !=
				layout->pll_slots[j])
				goto out;
			memcpy(image + layout->new_pll[j], old + offset,
			       bytes);
			member = layout->new_pll[j];
			memcpy(image + layout->new_members + 2 * j,
			       &member, sizeof(member));
		}
		target[i].o_members = layout->new_members;
		target[i].o_tables = layout->new_params;
		target[i].o_ratevolt = layout->new_rates;
	}

	/* All G3D rows and PLL slots come from the interface catalog. */
	g3d = cmucal_get_node(ACPM_VCLK_TYPE | SOC_FVMAP_G3D_DOMAIN);
	layout = exynos_soc_fvmap_layout_get(SOC_FVMAP_G3D_DOMAIN);
	params = exynos_soc_catalog_find("DVFS", "dvfs_g3d", "params",
					EXYNOS_SOC_DVFS_PARAMS);
	pll_rates = exynos_soc_catalog_find("PLL", "PLL_G3D", "rates",
					  EXYNOS_SOC_PLL_RATES);
	if (!g3d || !g3d->lut || g3d->num_rates != 13 ||
	    !params || params->rows != 13 || params->cols != 1 ||
	    !pll_rates || pll_rates->rows != 13 || pll_rates->cols != 5 ||
	    layout->new_pll[0] != 0x860)
		goto out;
	group = cal_asv_get_grp(g3d->id);
	if (group < 0)
		goto out;
	g3d_rates = (struct rate_volt *)(image + layout->new_rates);
	for (i = 0; i < 13; i++) {
		ret = exynos_soc_get_opp("dvfs_g3d", version, group, i,
					 &freq, &volt);
		if (ret || g3d->lut[i].rate != freq ||
		    params->data[i] != i) {
			if (!ret)
				ret = -EINVAL;
			goto out;
		}
		g3d_rates[i].rate = freq;
		g3d_rates[i].volt = volt;
		image[layout->new_params + i] = i;
	}
	ret = -EINVAL;
	if (((struct pll_header *)(old + 0x860))->addr != 0xaa240140 ||
	    (((struct pll_header *)(old + 0x860))->pms[0] != 0x00740400 &&
	     ((struct pll_header *)(old + 0x860))->pms[0] != 0x01900d00 &&
	     ((struct pll_header *)(old + 0x860))->pms[0] != 0x02580d00) ||
	    ((struct pll_header *)(old + 0x860))->pms[1] != 0x006c0400)
		goto out;
	for (i = 0; i < 13; i++) {
		const u64 *row = &pll_rates->data[i * 5];

		if (!row[1] || row[1] >= 64 || row[2] >= 1024 ||
		    row[3] >= 8 || row[4] ||
		    row[0] != 26000000ULL * row[2] / (row[1] << row[3]))
			goto out;
		pms = (row[2] << 16) | (row[1] << 8) | row[3];
		((struct pll_header *)(image + 0x860))->pms[i] = pms;
	}
	target[SOC_FVMAP_G3D_DOMAIN].num_of_lv = 13;
	/* Publish relocated payload before its headers point to the new offsets. */
	memcpy_toio(sram_base + sizeof(struct fvmap_header) *
			     exynos_soc_fvmap_layout_count(),
		    image + sizeof(struct fvmap_header) *
			    exynos_soc_fvmap_layout_count(),
		    SOC_FVMAP_PAYLOAD_END - sizeof(struct fvmap_header) *
					    exynos_soc_fvmap_layout_count());
	wmb();
	memcpy_toio(sram_base, image,
		    sizeof(struct fvmap_header) *
		    exynos_soc_fvmap_layout_count());
	wmb();
	pr_info("fvmap: installed 13-level G3D interface layout in 0x9f0-byte SRAM payload\n");
	ret = 0;
out:
	kfree(image);
	kfree(old);
	return ret;
}

static int fvmap_validate_g3d_pll(void __iomem *sram_base,
				  const volatile struct fvmap_header *header)
{
	const struct exynos_soc_catalog_table *rates;
	unsigned int pll_offset, i;
	u32 current_pms, expected_pms;
	const u64 *row;

	if (header->num_of_lv != 13 || header->num_of_members != 1 ||
	    header->num_of_pll != 1 ||
	    !fvmap_dump_region_valid(header->o_members, sizeof(u16)) ||
	    !fvmap_dump_region_valid(header->o_tables, 13) ||
	    !fvmap_dump_region_valid(header->o_ratevolt,
				     13 * sizeof(struct rate_volt)))
		return -EINVAL;
	for (i = 0; i < 13; i++) {
		if (readb(sram_base + header->o_tables + i) !=
		    i)
			return -EINVAL;
	}

	pll_offset = readw(sram_base + header->o_members);
	if (!fvmap_dump_region_valid(pll_offset,
				     sizeof(struct pll_header) + 13 * sizeof(u32)) ||
	    readl(sram_base + pll_offset) != 0xaa240140 ||
	    readw(sram_base + pll_offset +
		  offsetof(struct pll_header, level)) != 13)
		return -EINVAL;

	rates = exynos_soc_catalog_find("PLL", "PLL_G3D", "rates",
					EXYNOS_SOC_PLL_RATES);
	if (!rates || rates->rows != 13 || rates->cols != 5)
		return -EINVAL;

	for (i = 0; i < rates->rows; i++) {
		row = &rates->data[i * rates->cols];
		if (!row[1] || row[1] >= 64 || row[2] >= 1024 ||
		    row[3] >= 8 || row[4] ||
		    row[0] != 26000000ULL * row[2] / (row[1] << row[3]))
			return -EINVAL;
		expected_pms = (row[2] << 16) | (row[1] << 8) | row[3];
		current_pms = readl(sram_base + pll_offset +
				    offsetof(struct pll_header, pms) + i * sizeof(u32));
		if (current_pms != expected_pms)
			return -EINVAL;
	}
	return 0;
}

static void fvmap_copy_from_sram(void __iomem *map_base, void __iomem *sram_base)
{
	volatile struct fvmap_header *fvmap_header, *header;
	struct rate_volt_header *old, *new;
	struct dvfs_table *old_param, *new_param;
	struct clocks *clks;
	struct pll_header *plls;
	struct vclk *vclk;
	unsigned int member_addr;
	unsigned int blk_idx, param_idx;
	unsigned int rates[SOC_FVMAP_MAX_LEVELS];
	unsigned int volts[SOC_FVMAP_MAX_LEVELS];
	unsigned int version = cal_asv_get_tablever();
	int group, ret;
	int size, margin;
	int i, j, k;

	fvmap_header = map_base;
	header = sram_base;

	size = cmucal_get_list_size(ACPM_VCLK_TYPE);

	for (i = 0; i < size; i++) {
		/* load fvmap info */
		fvmap_header[i].dvfs_type = header[i].dvfs_type;
		fvmap_header[i].num_of_lv = header[i].num_of_lv;
		fvmap_header[i].num_of_members = header[i].num_of_members;
		fvmap_header[i].num_of_pll = header[i].num_of_pll;
		fvmap_header[i].num_of_mux = header[i].num_of_mux;
		fvmap_header[i].num_of_div = header[i].num_of_div;
		fvmap_header[i].gearratio = header[i].gearratio;
		fvmap_header[i].init_lv = header[i].init_lv;
		fvmap_header[i].num_of_gate = header[i].num_of_gate;
		fvmap_header[i].reserved[0] = header[i].reserved[0];
		fvmap_header[i].reserved[1] = header[i].reserved[1];
		fvmap_header[i].block_addr[0] = header[i].block_addr[0];
		fvmap_header[i].block_addr[1] = header[i].block_addr[1];
		fvmap_header[i].block_addr[2] = header[i].block_addr[2];
		fvmap_header[i].o_members = header[i].o_members;
		fvmap_header[i].o_ratevolt = header[i].o_ratevolt;
		fvmap_header[i].o_tables = header[i].o_tables;

		vclk = cmucal_get_node(ACPM_VCLK_TYPE | i);
		if (vclk == NULL)
			continue;
		if (i >= ARRAY_SIZE(soc_fvmap_ready) || !vclk->lut ||
		    fvmap_header[i].num_of_lv > SOC_FVMAP_MAX_LEVELS ||
		    vclk->num_rates != fvmap_header[i].num_of_lv ||
		    vclk->num_list != fvmap_header[i].num_of_members) {
			pr_err("fvmap: %s interface/SRAM shape mismatch\n", vclk->name);
			continue;
		}
		group = cal_asv_get_grp(vclk->id);
		if (group < 0) {
			pr_err("fvmap: %s has no ASV group\n", vclk->name);
			continue;
		}
		for (j = 0; j < vclk->num_rates; j++) {
			ret = exynos_soc_get_opp(vclk->name, version, group, j,
						 &rates[j], &volts[j]);
			if (ret)
				break;
			if (vclk->lut[j].rate != rates[j])
				break;
		}
		if (j != vclk->num_rates) {
			pr_err("fvmap: %s interface OPP L%d unavailable\n",
				vclk->name, j);
			continue;
		}
		if (!strcmp(vclk->name, "dvfs_g3d")) {
			ret = fvmap_validate_g3d_pll(sram_base,
						     &fvmap_header[i]);
			if (ret) {
				pr_err("fvmap: G3D PLL SRAM layout is unsupported\n");
				continue;
			}
		}
		pr_info("dvfs_type : %s - id : %x\n",
			vclk->name, fvmap_header[i].dvfs_type);
		pr_info("  num_of_lv      : %d\n", fvmap_header[i].num_of_lv);
		pr_info("  num_of_members : %d\n", fvmap_header[i].num_of_members);

		old = sram_base + fvmap_header[i].o_ratevolt;
		new = map_base + fvmap_header[i].o_ratevolt;

		margin = init_margin_table[vclk->margin_id];
		if (margin)
			cal_dfs_set_volt_margin(i | ACPM_VCLK_TYPE, margin);

		for (j = 0; j < fvmap_header[i].num_of_members; j++) {
			clks = sram_base + fvmap_header[i].o_members;

			if (j < fvmap_header[i].num_of_pll) {
				plls = sram_base + clks->addr[j];
				member_addr = plls->addr - 0x90000000;
			} else {

				member_addr = (clks->addr[j] & ~0x3) & 0xffff;
				blk_idx = clks->addr[j] & 0x3;

				if (blk_idx < BLOCK_ADDR_SIZE)
					member_addr |= ((fvmap_header[i].block_addr[blk_idx]) << 16) - 0x90000000;
				else
					pr_err("[%s] blk_idx %u is out of range for block_addr\n", __func__, blk_idx);
			}


			vclk->list[j] = cmucal_get_id_by_addr(member_addr);

			if (vclk->list[j] == INVALID_CLK_ID)
				pr_info("  Invalid addr :0x%x\n", member_addr);
			else
				pr_info("  DVFS CMU addr:0x%x\n", member_addr);
		}

		for (j = 0; j < fvmap_header[i].num_of_lv; j++) {
			new->table[j].rate = rates[j];
			new->table[j].volt = volts[j];
		}
		check_percent_margin(new, fvmap_header[i].num_of_lv);
		for (j = 0; j < fvmap_header[i].num_of_lv; j++) {
			old->table[j].rate = new->table[j].rate;
			old->table[j].volt = new->table[j].volt;
			pr_info("  lv : [%7d], volt = %d uV (%d %%) \n",
				new->table[j].rate, new->table[j].volt,
				volt_offset_percent);
		}

		old_param = sram_base + fvmap_header[i].o_tables;
		new_param = map_base + fvmap_header[i].o_tables;
		for (j = 0; j < fvmap_header[i].num_of_lv; j++) {
			for (k = 0; k < fvmap_header[i].num_of_members; k++) {
				param_idx = fvmap_header[i].num_of_members * j + k;
				new_param->val[param_idx] = vclk->lut[j].params[k];
				old_param->val[param_idx] = new_param->val[param_idx];
			}
		}
		soc_fvmap_ready[i] = true;
		exynos_soc_note_consumer(EXYNOS_SOC_CONSUMER_FVMAP);
		exynos_soc_note_fvmap_domain(vclk->name);
	}
}

int fvmap_init(void __iomem *sram_base)
{
	void __iomem *map_base;
	struct kobject *kobj;
	int ret;

	map_base = kzalloc(FVMAP_SIZE, GFP_KERNEL);
	if (!map_base)
		return -ENOMEM;

	fvmap_base = map_base;
	sram_fvmap_base = sram_base;
	pr_info("%s:fvmap initialize %pK\n", __func__, sram_base);
	ret = fvmap_install_interface_layout(sram_base);
	if (ret)
		pr_err("fvmap: 13-level G3D SRAM layout rejected: %d\n", ret);
	fvmap_copy_from_sram(map_base, sram_base);

	kobj = kobject_create_and_add("fvmap", kernel_kobj);
	if (!kobj) {
		pr_err("fvmap: failed to create /sys/kernel/fvmap\n");
	} else {
		ret = sysfs_create_bin_file(kobj, &fvmap_sram_dump_attr);
		if (ret) {
			pr_err("fvmap: failed to create sram_dump: %d\n", ret);
			kobject_put(kobj);
		} else {
			ret = sysfs_create_bin_file(kobj, &fvmap_sram_raw_attr);
			if (ret)
				pr_err("fvmap: failed to create sram_raw: %d\n", ret);
		}
	}

	/* percent margin for each doamin at runtime */
	kobj = kobject_create_and_add("percent_margin", power_kobj);
	if (!kobj) {
		pr_err("Fail to create percent_margin kboject\n");
		return 0;
	}
	if (sysfs_create_group(kobj, &percent_margin_group))
		pr_err("Fail to create percent_margin group\n");

	return 0;
}
