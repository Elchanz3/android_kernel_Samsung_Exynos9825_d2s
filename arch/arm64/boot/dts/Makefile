# SPDX-License-Identifier: GPL-2.0
dtb-y += exynos/exynos9820.dtb

dtbo-y += samsung/exynos9820-beyond0lte_eur_open_24.dtbo
dtbo-y += samsung/exynos9820-beyond0lte_eur_open_22.dtbo
dtbo-y += samsung/exynos9820-beyond0lte_eur_open_19.dtbo
dtbo-y += samsung/exynos9820-beyond0lte_eur_open_18.dtbo
dtbo-y += samsung/exynos9820-beyond0lte_eur_open_17.dtbo
dtbo-y += samsung/exynos9820-beyond0lte_eur_open_20.dtbo
dtbo-y += samsung/exynos9820-beyond0lte_eur_open_25.dtbo

dtbo-y += samsung/exynos9820-d1_eur_open_23.dtbo
dtbo-y += samsung/exynos9820-d1_eur_open_21.dtbo
dtbo-y += samsung/exynos9820-d1_eur_open_22.dtbo
dtbo-y += samsung/exynos9820-d1_eur_open_18.dtbo
dtbo-y += samsung/exynos9820-d1_eur_open_19.dtbo

dtbo-y += samsung/exynos9820-d2_eur_open_19.dtbo
dtbo-y += samsung/exynos9820-d2_eur_open_18.dtbo
dtbo-y += samsung/exynos9820-d2_eur_open_17.dtbo
dtbo-y += samsung/exynos9820-d2_eur_open_24.dtbo
dtbo-y += samsung/exynos9820-d2_eur_open_02.dtbo
dtbo-y += samsung/exynos9820-d2_eur_open_22.dtbo
dtbo-y += samsung/exynos9820-d2_eur_open_16.dtbo
dtbo-y += samsung/exynos9820-d2_eur_open_21.dtbo
dtbo-y += samsung/exynos9820-d2_eur_open_23.dtbo
dtbo-y += samsung/exynos9820-d2_eur_open_20.dtbo

dtbo-y += samsung/exynos9820-d2x_eur_open_20.dtbo
dtbo-y += samsung/exynos9820-d2x_eur_open_19.dtbo
dtbo-y += samsung/exynos9820-d2x_eur_open_17.dtbo
dtbo-y += samsung/exynos9820-d2x_eur_open_24.dtbo
dtbo-y += samsung/exynos9820-d2x_eur_open_16.dtbo
dtbo-y += samsung/exynos9820-d2x_eur_open_21.dtbo
dtbo-y += samsung/exynos9820-d2x_eur_open_02.dtbo
dtbo-y += samsung/exynos9820-d2x_eur_open_18.dtbo
dtbo-y += samsung/exynos9820-d2x_eur_open_22.dtbo
dtbo-y += samsung/exynos9820-d2x_eur_open_23.dtbo

targets += dtbs
DTB_LIST  := $(dtb-y) $(dtbo-y)
always    := $(DTB_LIST)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := *.dtb*
