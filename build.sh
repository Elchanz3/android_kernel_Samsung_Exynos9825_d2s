#!/bin/bash

clear

rm KernelSU

gio trash KernelSU

./setup-kernelsu.sh

mkdir out

DTB_DIR=$(pwd)/out/arch/arm64/boot/dts
mkdir ${DTB_DIR}/exynos

export PLATFORM_VERSION=13
export ANDROID_MAJOR_VERSION=t
export SEC_BUILD_CONF_VENDOR_BUILD_OS=13

BUILD_CROSS_COMPILE=/home/chanz22/toolchains/aarch64-zyc-linux-gnu-13/bin/aarch64-zyc-linux-gnu-
KERNEL_LLVM_BIN=/home/chanz22/toolchains/Clang-17.0.0/bin/clang
CLANG_TRIPLE=/home/chanz22/toolchains/aarch64-zyc-linux-gnu-13/bin/aarch64-zyc-linux-gnu-

DATE_START=$(date +"%s")

make O=out ARCH=arm64 CC=$KERNEL_LLVM_BIN exynos9820-d2s_defconfig
make O=out ARCH=arm64 \
	CROSS_COMPILE=$BUILD_CROSS_COMPILE CC=$KERNEL_LLVM_BIN \
	CLANG_TRIPLE=$CLANG_TRIPLE -j8 2>&1 |tee ../compile-Weibo.log

# remove a previous kernel image
rm $IMAGE &> /dev/null

$(pwd)/tools/mkdtimg cfg_create $(pwd)/out/dtb.img dt.configs/exynos9820.cfg -d ${DTB_DIR}/exynos

IMAGE="out/arch/arm64/boot/Image"
if [[ -f "$IMAGE" ]]; then
        KERNELZIP="WeiboKernel.zip"
	rm AnyKernel3/zImage > /dev/null 2>&1
	rm AnyKernel3/dtb > /dev/null 2>&1
	rm AnyKernel3/*.zip > /dev/null 2>&1
	mv out/dtb.img AnyKernel3/dtb
	mv $IMAGE AnyKernel3/zImage
	cd AnyKernel3
	zip -r9 $KERNELZIP .
	
	DATE_END=$(date +"%s")
	DIFF=$(($DATE_END - $DATE_START))

	echo -e "\nTime elapsed: $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds.\n"
	
	
fi
