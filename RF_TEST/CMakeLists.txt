#############################################################[Chinese Manual]###################################################################
# RF_TEST —— CH570Q 射频测试固件（定频/单信道发射）
#
# 构建方式与仓库内另外两个工程一致（Linux + CMake + 仓库自带工具链）：
#   cd RF_TEST
#   cmake -B build -G "Unix Makefiles"
#   cmake --build build -j"$(nproc)"
#
# 产物：build/RF_TEST.elf / RF_TEST.hex / RF_TEST.map / RF_TEST.lst
# 与 RF_Uart（从机）、RF_UartDongle（主机）一样，这是**独立固件**，
# 烧到任意一块 CH570Q 板子上都能跑（不参与正常通信流程）。
#################################################################################################################################################

# Set CMake minimum require
cmake_minimum_required(VERSION 3.16)

# Set CMake sysytem name
set(CMAKE_SYSTEM_NAME Generic)

# Skipping compiler detection and use custom toolchain
SET(CMAKE_C_COMPILER_WORKS TRUE)
SET(CMAKE_CXX_COMPILER_WORKS TRUE)
SET(CMAKE_ASM_COMPILER_WORKS TRUE)

# 工具链根目录：默认为仓库内的 tools/toolchain 子模块（沁恒定制的 riscv-wch-elf GCC 12.2.0，Linux x64）。
# 必须是沁恒定制版：-march 的 xw 扩展（mcpy 等自定义指令）只有它支持。
set(TOOLCHAIN_FOLDER "${CMAKE_CURRENT_LIST_DIR}/../tools/toolchain" CACHE PATH "RISC-V 工具链根目录（默认：仓库内 tools/toolchain 子模块）")
set(TOOLCHAIN_PREFIX "riscv-wch-elf-")
set(CMAKE_C_COMPILER "${TOOLCHAIN_FOLDER}/bin/riscv-wch-elf-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_FOLDER}/bin/riscv-wch-elf-g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_FOLDER}/bin/riscv-wch-elf-gcc")
set(CMAKE_OBJCOPY "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}objcopy")
set(CMAKE_OBJDUMP "${TOOLCHAIN_FOLDER}/bin/riscv-wch-elf-objdump")
set(CMAKE_SIZE "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}size")
set(CMAKE_AR "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}ar")

set(PROJECT_NAME "RF_TEST")
set(TARGET_ARTIFACT "RF_TEST.elf")

set(CMAKE_C_OUTPUT_EXTENSION .o)
set(CMAKE_CXX_OUTPUT_EXTENSION .o)

project("${PROJECT_NAME}" LANGUAGES C ASM CXX)

# Use relative path
add_compile_options(-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=..)


set(SRC_FILES 
"${CMAKE_CURRENT_SOURCE_DIR}/APP/main.c"
"${CMAKE_CURRENT_SOURCE_DIR}/APP/rf_test.c"
"${CMAKE_CURRENT_SOURCE_DIR}/APP/uart_cmd.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_clk.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_cmp.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_flash.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_gpio.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_i2c.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_keyscan.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_pwm.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_pwr.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_spi.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_sys.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_timer.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_uart.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_usbdev.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_usbhostBase.c"
"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/CH57x_usbhostClass.c"
"${CMAKE_CURRENT_SOURCE_DIR}/Startup/startup_CH572.S"
)
set(CMAKE_C_FLAGS " -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -I\"${CMAKE_CURRENT_SOURCE_DIR}/Startup/\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/APP/include\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/inc\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/Ld/\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/LIB/\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/RVMSIS/\" -std=gnu99")
set(CMAKE_CXX_FLAGS " -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -std=gnu++11 -fabi-version=0")
set(CMAKE_ASM_FLAGS " -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -x assembler-with-cpp")

# 默认测试频点（定频信道号 0-39，f = 2402 + 2*ch）：
#   36 → 2474 MHz、37 → 2476 MHz、38 → 2478 MHz
#   （正好对应应用频段 CH_HOP_TBL = {74,76,78}，见 APP/include/rf_test.h）
# 配置阶段可覆盖，例如生成 2474 MHz 的固件：
#   cmake -B build_36 -S . -G "Unix Makefiles" -DRFTEST_DEF_CHANNEL=36
set(RFTEST_DEF_CHANNEL "37" CACHE STRING "RF_TEST 默认测试频点（定频信道号 0-39，f = 2402 + 2*ch）")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRFTEST_DEF_CHANNEL=${RFTEST_DEF_CHANNEL}")

add_executable("${TARGET_ARTIFACT}" ${SRC_FILES})

set_target_properties("${TARGET_ARTIFACT}" PROPERTIES LINK_FLAGS " -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -T \"${CMAKE_CURRENT_SOURCE_DIR}/Ld/Link.ld\"  -L\"${CMAKE_CURRENT_SOURCE_DIR}/\" -L\"${CMAKE_CURRENT_SOURCE_DIR}/LIB/\" -L\"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/\"  -nostartfiles -Xlinker --gc-sections -Xlinker --print-memory-usage -Wl,-Map,\"RF_TEST.map\" --specs=nano.specs --specs=nosys.specs -o \"RF_TEST.elf\" ")

target_link_libraries(
"${TARGET_ARTIFACT}" 
ISP572
m
CH57xRF
)

add_custom_command(TARGET "${TARGET_ARTIFACT}" 
POST_BUILD 
COMMAND ${CMAKE_OBJCOPY} ARGS -O ihex  "RF_TEST.elf" "RF_TEST.hex"
COMMAND ${CMAKE_OBJDUMP} ARGS  --source --all-headers --demangle -M xw --line-numbers --wide "RF_TEST.elf" > "RF_TEST.lst"
COMMAND ${CMAKE_SIZE} ARGS  --format=berkeley "RF_TEST.elf"
)
