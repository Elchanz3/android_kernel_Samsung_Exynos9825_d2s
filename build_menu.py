import os

def clear_screen():
    os.system("clear")

def compile_kernel():
    clear_screen()
    print("Compiling the kernel...\n")
    
    DIR = os.path.abspath('.')
    PARENT_DIR = os.path.abspath(os.path.join(DIR, '..'))
    DEFCONFIG_NAME = 'exynos9820-d2s_defconfig'
    CHIPSET_NAME = 'universal9820'
    VARIANT = 'd2s'
    ARCH = 'arm64'
    VERSION = f'WeiboKernel_{VARIANT}_v0.8.2'
    LOG_FILE = 'compilation.log'
    
    # Create the necessary folders
    os.makedirs('out', exist_ok=True)
    DTB_DIR = os.path.join(os.getcwd(), 'out', 'arch', 'arm64', 'boot', 'dts')
    os.makedirs(os.path.join(DTB_DIR, 'exynos'), exist_ok=True)
    
    PLATFORM_VERSION = '13'
    ANDROID_MAJOR_VERSION = 't'
    SEC_BUILD_CONF_VENDOR_BUILD_OS = '13'
    
    BUILD_CROSS_COMPILE = '/home/chanz22/toolchains/aarch64-zyc-linux-gnu-14/bin/aarch64-zyc-linux-gnu-'
    KERNEL_LLVM_BIN = '/home/chanz22/toolchains/Clang-18.0.0-20231004/bin/clang'
    CLANG_TRIPLE = '/home/chanz22/toolchains/aarch64-zyc-linux-gnu-14/bin/aarch64-zyc-linux-gnu-'
    
    DATE_START = os.popen("date +'%s'").read().strip()
    
    make_defconfig_cmd = f"make O=out ARCH=arm64 CC={KERNEL_LLVM_BIN} {DEFCONFIG_NAME}"
    make_kernel_cmd = f"make O=out ARCH=arm64 CROSS_COMPILE={BUILD_CROSS_COMPILE} CC={KERNEL_LLVM_BIN} CLANG_TRIPLE={CLANG_TRIPLE} -j12 2>&1 | tee ../{LOG_FILE}"
    
    # Execute the commands
    os.system(make_defconfig_cmd)
    os.system(make_kernel_cmd)
    
    # Remove the previous kernel image, if any
    IMAGE = "out/arch/arm64/boot/Image"
    if os.path.exists(IMAGE):
        os.remove(IMAGE)
    
    dtb_img_cmd = f"{os.getcwd()}/tools/mkdtimg cfg_create {os.path.join(os.getcwd(), 'out', 'dtb.img')} dt.configs/exynos9820.cfg -d {DTB_DIR}/exynos"
    os.system(dtb_img_cmd)
    
    if os.path.exists(IMAGE):
        KERNELZIP = f"{VERSION}.zip"
        os.system(f"rm AnyKernel3/zImage > /dev/null 2>&1")
        os.system(f"rm AnyKernel3/dtb > /dev/null 2>&1")
        os.system(f"rm AnyKernel3/*.zip > /dev/null 2>&1")
        os.system(f"mv out/dtb.img AnyKernel3/dtb")
        os.system(f"mv {IMAGE} AnyKernel3/zImage")
        os.chdir("AnyKernel3")
        os.system(f"zip -r9 {KERNELZIP} .")
        os.chdir("..")
    
        DATE_END = os.popen("date +'%s'").read().strip()
        DIFF = int(DATE_END) - int(DATE_START)
    
        print(f"\nTime elapsed: {DIFF // 60} minute(s) and {DIFF % 60} seconds.\n")
    
    input("\nPress Enter to continue...")

def create_dtb_image():
    clear_screen()
    print("Creating the DTB image...\n")
    
    # Commands to create DTB image
    dtb_image_cmd = """
    $(pwd)/tools/mkdtimg cfg_create $(pwd)/out/dtb.img dt.configs/exynos9820.cfg -d ${DTB_DIR}/exynos
    """
    os.system(dtb_image_cmd)
    
    input("\nPressione Enter para continuar...")

def main_menu():
    while True:
        clear_screen()
        print("Main Menu:")
        print("1. Compile the kernel")
        print("2. Create DTB image")
        print("3. exit")
        
        choice = input("Escolha uma opção: ")
        
        if choice == '1':
            compile_kernel()
        elif choice == '2':
            create_dtb_image()
        elif choice == '3':
            clear_screen()
            print("Exiting...")
            break
        else:
            input("\nInvalid option. Press Enter to continue...")

if __name__ == "__main__":
    main_menu()
