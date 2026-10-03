#############################################################[Chinese Manual]###################################################################
# *****工程路径不要包含中文字符和空格,否则可能会报错！*****
# 第一步： 确认机器上已经安装了MRS的工具链(假设当前平台为Linux,工具链路径为"/path/to/toolchain/RISC-V Embedded GCC12",工具链相关前缀为"riscv-wch-elf-")
# 第二步: 设置工具链路径,例如修改替换下方的set(TOOLCHAIN_FOLDER "/path/to/toolchain/RISC-V Embedded GCC12")
# 第三步: 设置工具链中相关可执行文件的前缀,例如修改替换下方的set(TOOLCHAIN_PREFIX "riscv-wch-elf-")
# 第四步: 执行 cd your/path/of/project
# 第五步: 执行 cmake -B build -G "Unix Makefiles"
# 第六步: 执行 cmake --build build
# 注意: 在Windows平台运行,需要a.安装GNU MAKE并将其路径(bin目录)添加到系统环境变量,之后执行cmake -B build -G "Unix Makefiles"(推荐)
#       或者b.安装MinGW,将其路径(bin目录)添加到系统环境变量,之后执行cmake -B build -G "MinGW Makefiles"
# 如有相关疑问,可通过support@mounriver.com联系我们
#################################################################################################################################################


#############################################################[English Manual]###################################################################
# Step 1: Confirm that the MRS toolchain has been installed on the machine. Assuming the current platform is Linux, 
#         the toolchain path is "/path/to/toolchain/RISC-V Embedded GCC12", and the toolchain-related prefix is "riscv-wch-elf-"
# Step 2: Set the toolchain path, for example, modify and replace the following "set (TOOLCHAIN_FOLDER "/path/to/toolchain/RISC-V Embedded GCC12")"
# Step 3: Set the prefix of the executable files in the toolchain,for example, modify and replace the following "set(TOOLCHAIN_PREFIX "riscv-wch-elf-")"
# Step 4: Call "cd your/path/of/project"
# Step 5: Call "cmake -B build -G "Unix Makefiles""
# Step 6: Call "cmake --build build"
# Note:   To run on Windows, you need: a. Install GNU MAKE and add its path (the bin directory) to the system environment PATH. 
#         Then call the execution of "cmake -B build -G "Unix Makefiles""(Recommanded).
#         or b. Install MinGW and add its path (the bin directory) to the system environment PATH. 
#         Then call the execution of "cmake -B build -G "MinGW Makefiles"".
# If you have any questions, please contact us at support@mounriver.com
#################################################################################################################################################


# Set CMake minimum require
cmake_minimum_required(VERSION 3.16)

# Set CMake sysytem name
set(CMAKE_SYSTEM_NAME Generic)


# Skipping compiler detection and use custom toolchain
SET(CMAKE_C_COMPILER_WORKS TRUE)
SET(CMAKE_CXX_COMPILER_WORKS TRUE)
SET(CMAKE_ASM_COMPILER_WORKS TRUE)

# Use MRS toolchian to compile
# 工具链根目录：默认为仓库内的 tools/toolchain 子模块（沁恒定制的 riscv-wch-elf GCC 12.2.0，Linux x64）。
# 必须是沁恒定制版：-march 的 xw 扩展（mcpy 等自定义指令）只有它支持。
# 首次使用请先初始化子模块：git submodule update --init --recursive
# 该变量是 CACHE 变量，可在配置阶段覆盖，例如指向 MounRiver Studio 自带工具链：
#   cmake -B build -G "Unix Makefiles" -DTOOLCHAIN_FOLDER="$HOME/MounRiver_Studio2/resources/app/resources/linux/components/WCH/Toolchain/RISC-V Embedded GCC12"
set(TOOLCHAIN_FOLDER "${CMAKE_CURRENT_LIST_DIR}/../tools/toolchain" CACHE PATH "RISC-V 工具链根目录（默认：仓库内 tools/toolchain 子模块）")
set(TOOLCHAIN_PREFIX "riscv-wch-elf-")
set(CMAKE_C_COMPILER "${TOOLCHAIN_FOLDER}/bin/riscv-wch-elf-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_FOLDER}/bin/riscv-wch-elf-g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_FOLDER}/bin/riscv-wch-elf-gcc")
set(CMAKE_OBJCOPY "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}objcopy")
set(CMAKE_OBJDUMP "${TOOLCHAIN_FOLDER}/bin/riscv-wch-elf-objdump")
set(CMAKE_SIZE "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}size")
set(CMAKE_AR "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}ar")

set(PROJECT_NAME "RF_Uart")
set(TARGET_ARTIFACT "RF_Uart.elf")

set(CMAKE_C_OUTPUT_EXTENSION .o)
set(CMAKE_CXX_OUTPUT_EXTENSION .o)

project("${PROJECT_NAME}" LANGUAGES C ASM CXX)

# Use relative path
add_compile_options(-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=..)


set(SRC_FILES 
"${CMAKE_CURRENT_SOURCE_DIR}/APP/buf.c"
"${CMAKE_CURRENT_SOURCE_DIR}/APP/main.c"
"${CMAKE_CURRENT_SOURCE_DIR}/APP/my_printf.c"
"${CMAKE_CURRENT_SOURCE_DIR}/APP/rf.c"
"${CMAKE_CURRENT_SOURCE_DIR}/APP/rf_uart_tx.c"
"${CMAKE_CURRENT_SOURCE_DIR}/APP/uart.c"
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
set(CMAKE_C_FLAGS " -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -I\"${CMAKE_CURRENT_SOURCE_DIR}/Startup/\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/APP/include\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/Profile/include\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/inc\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/Ld/\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/LIB/\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/RVMSIS/\" -std=gnu99")
set(CMAKE_CXX_FLAGS " -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -std=gnu++11 -fabi-version=0")
set(CMAKE_ASM_FLAGS " -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -x assembler-with-cpp")

add_executable("${TARGET_ARTIFACT}" ${SRC_FILES})

set_target_properties("${TARGET_ARTIFACT}" PROPERTIES LINK_FLAGS " -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -T \"${CMAKE_CURRENT_SOURCE_DIR}/Ld/Link.ld\"  -L\"${CMAKE_CURRENT_SOURCE_DIR}/\" -L\"${CMAKE_CURRENT_SOURCE_DIR}/LIB/\" -L\"${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/\"  -nostartfiles -Xlinker --gc-sections -Xlinker --print-memory-usage -Wl,-Map,\"RF_Uart.map\" --specs=nano.specs --specs=nosys.specs -o \"RF_Uart.elf\" ")

target_link_libraries(
"${TARGET_ARTIFACT}" 
ISP572
m
CH57xRF
)

add_custom_command(TARGET "${TARGET_ARTIFACT}" 
POST_BUILD 
COMMAND ${CMAKE_OBJCOPY} ARGS -O ihex  "RF_Uart.elf" "RF_Uart.hex"
COMMAND ${CMAKE_OBJDUMP} ARGS  --source --all-headers --demangle -M xw --line-numbers --wide "RF_Uart.elf" > "RF_Uart.lst"
COMMAND ${CMAKE_SIZE} ARGS  --format=berkeley "RF_Uart.elf"
)

# ============================================================================
# ISP 串口烧录（WCHISPTool_CMD）
#   cmake --build build --target flash          # 烧录
#   cmake --build build --target flash-verify   # 只校验
# 参数（-D 覆盖，如 -DISP_WAIT=30）：
#   ISP_PORT   串口设备，工具要求设备名为 ttyISPx（默认 /dev/ttyISP0）
#   ISP_WAIT   等待目标板上电的秒数（BOOT 检测在上电瞬间，默认 15）
#   ISP_SUDO   1=强制 sudo / 0=不提权 / 不设=自动
# ============================================================================
set(ISP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../skills/coder-ch570q-airttl/scripts/isp-flash.sh"
    CACHE FILEPATH "ISP 烧录脚本")
# Config.ini 优先用工程自带的（工程自包含）；没有才回退到 skill 里的模板
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Config.ini")
    set(_ISP_CONFIG_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/Config.ini")
else()
    set(_ISP_CONFIG_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../skills/coder-ch570q-airttl/scripts/Config.ini")
endif()
set(ISP_CONFIG "${_ISP_CONFIG_DEFAULT}" CACHE FILEPATH "ISP Config.ini（默认用工程自带）")

# 迁移：早期版本把 ISP_CONFIG 默认成了 skill 里那份模板，老 build 目录会残留该路径，
# 而 CACHE 变量不会因为改了默认值就自动更新 —— 若它仍指向 skill 模板且工程内已有，
# 就自动切到工程自带的那份（用户显式指定过其它路径则不动）。
get_filename_component(_ISP_CFG_REAL "${ISP_CONFIG}" REALPATH)
get_filename_component(_ISP_TPL_REAL "${CMAKE_CURRENT_SOURCE_DIR}/../skills/coder-ch570q-airttl/scripts/Config.ini" REALPATH)
if(_ISP_CFG_REAL STREQUAL _ISP_TPL_REAL
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Config.ini")
    set(ISP_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/Config.ini"
        CACHE FILEPATH "ISP Config.ini（默认用工程自带）" FORCE)
    message(STATUS "ISP: ISP_CONFIG 已从 skill 模板迁移为工程自带 -> ${ISP_CONFIG}")
endif()
set(ISP_PORT ""          CACHE STRING "ISP 串口设备；留空=由脚本自动查找（推荐）。\n                                        此处填**实际**串口（如 /dev/ttyACM0）；\n                                        工具要求的 ttyISPx 软链由脚本负责建立")
set(ISP_WAIT "15"           CACHE STRING "等待目标板上电的秒数")
set(ISP_BAUD "115200"       CACHE STRING "ISP 波特率")


# ---- 串口探测（仅在 configure 阶段给提示，便于选择）----
# ISP_PORT 留空时，烧录阶段由 isp-flash.sh 自己再查一次 /dev/ttyACM* 或 /dev/ttyUSB*，
# 所以配置时还没插设备也没关系。
if(NOT ISP_PORT)
    file(GLOB _isp_ports /dev/ttyACM* /dev/ttyUSB*)
    if(_isp_ports)
        message(STATUS "ISP: ISP_PORT 未指定 -> 交由脚本自动查找；当前可见: ${_isp_ports}")
    else()
        message(STATUS "ISP: ISP_PORT 未指定 -> 交由脚本自动查找；当前未检测到 /dev/ttyACM* 或 /dev/ttyUSB*")
    endif()
endif()

string(REPLACE ".elf" ".hex" ISP_HEX_NAME "${TARGET_ARTIFACT}")
set(ISP_HEX "${CMAKE_CURRENT_BINARY_DIR}/${ISP_HEX_NAME}")

# 指定了实际串口才传 -p；留空则由脚本自动查找 /dev/ttyACM* 或 /dev/ttyUSB*
# （若把软链名 /dev/ttyISP0 当实际串口传进来，脚本会跳过建链，工具反而拿不到真设备）
if(ISP_PORT)
    set(ISP_PORT_ARG -p "${ISP_PORT}")
else()
    set(ISP_PORT_ARG "")
endif()

add_custom_target(flash
    COMMAND "${ISP_SCRIPT}" -c "${ISP_CONFIG}" -f "${ISP_HEX}"
            ${ISP_PORT_ARG} -b "${ISP_BAUD}" -w "${ISP_WAIT}"
    DEPENDS "${TARGET_ARTIFACT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "ISP 烧录 ${ISP_HEX_NAME}（等待期内请给目标板上电）"
    USES_TERMINAL VERBATIM)

add_custom_target(flash-verify
    COMMAND "${ISP_SCRIPT}" -c "${ISP_CONFIG}" -f "${ISP_HEX}"
            ${ISP_PORT_ARG} -b "${ISP_BAUD}" -o verify -w "${ISP_WAIT}"
    DEPENDS "${TARGET_ARTIFACT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "ISP 校验 ${ISP_HEX_NAME}（等待期内请给目标板上电）"
    USES_TERMINAL VERBATIM)
