#!/bin/bash

mkdir out

DTB_DIR=$(pwd)/out/arch/arm64/boot/dts
mkdir ${DTB_DIR}/exynos

export PLATFORM_VERSION=13
export ANDROID_MAJOR_VERSION=t
export SEC_BUILD_CONF_VENDOR_BUILD_OS=13

make O=out ARCH=arm64 exynos9820-d2s_defconfig

DATE_START=$(date +"%s")

# remove a previous kernel image
rm $IMAGE &> /dev/null

make O=out ARCH=arm64 -j8

$(pwd)/tools/mkdtimg cfg_create $(pwd)/out/dtb.img dt.configs/exynos9820.cfg -d ${DTB_DIR}/exynos

IMAGE="out/arch/arm64/boot/Image"
if [[ -f "$IMAGE" ]]; then
        KERNELZIP="WeiBokernel-$(date +"%Y%m%d%H%M").zip"
	rm AnyKernel3/zImage > /dev/null 2>&1
	rm AnyKernel3/dtb > /dev/null 2>&1
	rm AnyKernel3/*.zip > /dev/null 2>&1
	mv out/dtb.img AnyKernel3/dtb
	mv $IMAGE AnyKernel3/zImage
	cd AnyKernel3
	zip -r9 zip -r9 $KERNELZIP .
	
	DATE_END=$(date +"%s")
	DIFF=$(($DATE_END - $DATE_START))

	echo -e "\nTime elapsed: $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds.\n"
	adb wait-for-recovery
	adb push $KERNELZIP /sdcard/
	
	#auto install kernel to your device
	adb sideload $KERNELZIP
	
else
	echo -e "ERROR\n"
fi
