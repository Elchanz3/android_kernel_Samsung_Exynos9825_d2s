#!/bin/bash

mkdir out

sudo apt-get update && apt-get install git ccache automake lzop bison gperf build-essential zip curl zlib1g-dev zlib1g-dev:i386 g++-multilib libxml2-utils bzip2 libbz2-dev libbz2-1.0 libghc-bzlib-dev squashfs-tools pngcrush schedtool dpkg-dev liblz4-tool make optipng -y

DTB_DIR=$(pwd)/out/arch/arm64/boot/dts
mkdir ${DTB_DIR}/exynos

export PLATFORM_VERSION=11
export ANDROID_MAJOR_VERSION=t
export SEC_BUILD_CONF_VENDOR_BUILD_OS=13

make O=out ARCH=arm64 exynos9820-d2s_defconfig

make O=out ARCH=arm64 -j8

$(pwd)/tools/mkdtimg cfg_create $(pwd)/out/dtb.img dt.configs/exynos9820.cfg -d ${DTB_DIR}/exynos

IMAGE="out/arch/arm64/boot/Image"
if [[ -f "$IMAGE" ]]; then
	rm AnyKernel3/zImage > /dev/null 2>&1
	rm AnyKernel3/dtb > /dev/null 2>&1
	rm AnyKernel3/*.zip > /dev/null 2>&1
	mv out/dtb.img AnyKernel3/dtb
	mv $IMAGE AnyKernel3/zImage
	cd AnyKernel3
	zip -r9 Kernel-N975F-$(date +"%Y%m%d%H%M").zip .
fi
