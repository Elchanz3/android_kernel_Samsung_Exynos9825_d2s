#!/bin/bash
set -e -o pipefail

# Variables
DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$DIR"

DEFCONFIG_NAME=exynos9820-d2s_defconfig
VARIANT=d2s
VERSION="${KERNEL_VERSION:-WeiboKernel_${VARIANT}_v0.8-redone}"
LOG_FILE=compilation.log

if [[ ! "$VERSION" =~ ^[[:alnum:]_.-]+$ ]]; then
	echo "KERNEL_VERSION must be a filename without directory components." >&2
	exit 1
fi

mkdir -p out
DTB_DIR="$DIR/out/arch/arm64/boot/dts"
mkdir -p "$DTB_DIR/exynos"

export PLATFORM_VERSION=13
export ANDROID_MAJOR_VERSION=t
export SEC_BUILD_CONF_VENDOR_BUILD_OS=13
export LLVM=1
export CCACHE_DISABLE=1
export KSU_VERSION_OVERRIDE=33004
export KSU_VERSION_TAG_OVERRIDE=v3.2.0-legacy
export KBUILD_BUILD_HOST="${KBUILD_BUILD_HOST:-$(uname -n)}"

BUILD_CROSS_COMPILE=/home/chanz22/tc/aarch64-zyc-linux-gnu-14/bin/aarch64-zyc-linux-gnu-
KERNEL_LLVM_BIN=/home/chanz22/tc/Clang-18.0.0git-20240124/bin/clang
CLANG_TRIPLE=$BUILD_CROSS_COMPILE

DATE_START=$(date +"%s")

make O=out ARCH=arm64 CC="$KERNEL_LLVM_BIN" "$DEFCONFIG_NAME"
make O=out ARCH=arm64 \
	CROSS_COMPILE="$BUILD_CROSS_COMPILE" CC="$KERNEL_LLVM_BIN" \
	CLANG_TRIPLE="$CLANG_TRIPLE" -j12 2>&1 | tee "$LOG_FILE"

"$DIR/tools/mkdtimg" cfg_create "$DIR/out/dtb.img" \
	dt.configs/exynos9820.cfg -d "$DTB_DIR/exynos"

IMAGE=out/arch/arm64/boot/Image
if [[ ! -s "$IMAGE" ]]; then
	echo "Kernel image was not produced: $IMAGE" >&2
	exit 1
fi

KERNELZIP="$VERSION.zip"
cp out/dtb.img AnyKernel3/dtb
cp "$IMAGE" AnyKernel3/zImage
cd AnyKernel3
rm -f "$KERNELZIP"
# Package installer inputs explicitly; extracted kernels and old ZIPs are excluded.
zip -r9 "$KERNELZIP" META-INF tools init modules anykernel.sh dtb zImage \
	LICENSE README.md

DATE_END=$(date +"%s")
DIFF=$((DATE_END - DATE_START))
printf '\nTime elapsed: %d minute(s) and %d seconds.\n' \
	"$((DIFF / 60))" "$((DIFF % 60))"
