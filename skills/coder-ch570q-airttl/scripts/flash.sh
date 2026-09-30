#!/usr/bin/env bash
#
# SeaHi-Serial-AirTTL 烧录脚本
#   目标芯片: 沁恒 CH570Q（RISC-V），调试器: WCH-Link / WCH-LinkE（SDI 接口）
#   依赖: WCH 定制版 OpenOCD（带 wlinke 驱动的 wch-riscv target）
#
# 用法见 --help。
#
set -euo pipefail

# ---------- 默认配置 ----------
CFG="${OPENOCD_CFG:-}"
MODE="verify"
FIRMWARE=""
# 找一个可用的 WCH 定制版 OpenOCD：环境变量 > PATH > MounRiver Studio 自带
if [ -z "${OPENOCD_BIN:-}" ]; then
    if command -v openocd >/dev/null 2>&1; then
        OPENOCD_BIN="openocd"
    else
        for cand in \
            "$HOME/MounRiver_Studio2/toolchain/OpenOCD/bin/openocd" \
            "/opt/MounRiver_Studio2/toolchain/OpenOCD/bin/openocd"
        do
            [ -x "$cand" ] && { OPENOCD_BIN="$cand"; break; }
        done
        [ -z "${OPENOCD_BIN:-}" ] && OPENOCD_BIN="openocd"   # 找不到也先赋值，由后面统一报错
    fi
fi

usage() {
    cat <<'EOF'
用法: flash.sh [选项] [<firmware.hex>]

选项:
  -c, --cfg <path>     OpenOCD target 配置路径（默认自动查找 wch-riscv.cfg）
  -m, --mode <mode>    烧录模式，默认 verify
  -h, --help           显示本帮助

模式:
  verify            烧录 + 校验 + 复位（默认，日常用这个）
  erase-program     先擦除再烧录 + 校验（program 失败时用）
  unlock-program    解除读保护后烧录 + 校验（报 flash protected 时用）
  verify-only       只校验 Flash，不烧录
  erase-all         全片擦除（会清掉从机绑定信息，需重新贴近配对）
  reset             只复位并停住（连不上时先把芯片停下来）

示例:
  flash.sh RF_Uart/build/RF_Uart.hex
  flash.sh -m unlock-program RF_Uart/build/RF_Uart.hex
  flash.sh -m reset
  flash.sh -c /path/to/wch-riscv.cfg -m verify-only RF_Uart/build/RF_Uart.hex

注意:
  两个固件运行时都会关闭两线调试功能，连不上时请先给目标板断电重上电。
EOF
}

# ---------- 参数解析 ----------
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        -c|--cfg)  CFG="${2:?--cfg 需要一个路径}"; shift 2 ;;
        -m|--mode) MODE="${2:?--mode 需要一个值}"; shift 2 ;;
        -*)        echo "未知选项: $1" >&2; usage >&2; exit 2 ;;
        *)         FIRMWARE="$1"; shift ;;
    esac
done

# ---------- 查找 OpenOCD 配置 ----------
if [ -z "$CFG" ]; then
    for c in \
        /usr/local/share/openocd/scripts/target/wch-riscv.cfg \
        /usr/share/openocd/scripts/target/wch-riscv.cfg \
        "$HOME/MounRiver_Studio2/toolchain/OpenOCD/bin/wch-riscv.cfg" \
        "$(dirname "$0")/wch-riscv.cfg"
    do
        if [ -f "$c" ]; then CFG="$c"; break; fi
    done
fi
if [ -z "$CFG" ] || [ ! -f "$CFG" ]; then
    echo "❌ 找不到 OpenOCD 配置 wch-riscv.cfg" >&2
    echo "   需要一份含 wlinke 驱动的 WCH 定制版 OpenOCD（发行版自带的没有该驱动）：" >&2
    echo "     · 系统已装：/usr/local/share/openocd/scripts/target/wch-riscv.cfg" >&2
    echo "     · MounRiver Studio 自带：\$MRS_HOME/toolchain/OpenOCD/bin/" >&2
    echo "   也可用 -c <path> 或环境变量 OPENOCD_CFG 指定其它位置的 wch-riscv.cfg" >&2
    exit 1
fi

command -v "$OPENOCD_BIN" >/dev/null 2>&1 || {
    echo "❌ 找不到 openocd 可执行文件：$OPENOCD_BIN" >&2; exit 1; }

# ---------- 固件检查（reset 模式除外） ----------
NEED_FW=1
[ "$MODE" = "reset" ] && NEED_FW=0
if [ "$NEED_FW" = "1" ]; then
    if [ -z "$FIRMWARE" ]; then
        echo "❌ 请指定固件文件（.hex）" >&2; usage >&2; exit 2
    fi
    if [ ! -f "$FIRMWARE" ]; then
        echo "❌ 固件不存在: $FIRMWARE" >&2
        echo "   先编译：cd <工程> && cmake -B build -G 'Unix Makefiles' && cmake --build build" >&2
        exit 1
    fi
    FIRMWARE="$(realpath "$FIRMWARE")"
fi

echo "▶ OpenOCD : $OPENOCD_BIN"
echo "▶ 配置    : $CFG"
echo "▶ 模式    : $MODE"
[ "$NEED_FW" = "1" ] && echo "▶ 固件    : $FIRMWARE"
echo

# ---------- 按模式构造命令 ----------
case "$MODE" in
    verify)
        CMDS=(-c "init" -c "halt"
              -c "program \"$FIRMWARE\" verify"
              -c "reset" -c "exit") ;;
    erase-program)
        CMDS=(-c "init" -c "halt"
              -c "flash write_image erase \"$FIRMWARE\""
              -c "verify_image \"$FIRMWARE\""
              -c "reset" -c "exit") ;;
    unlock-program)
        CMDS=(-c "init" -c "halt"
              -c "flash erase_address unlock 0x00000000 0x10000"
              -c "flash write_image \"$FIRMWARE\""
              -c "flash verify_image \"$FIRMWARE\""
              -c "reset" -c "exit") ;;
    verify-only)
        CMDS=(-c "init" -c "halt"
              -c "verify_image \"$FIRMWARE\""
              -c "exit") ;;
    erase-all)
        echo "⚠️  全片擦除会清掉从机 Flash 里的绑定信息，之后需要重新贴近配对。"
        read -r -p "确认继续？[y/N] " a
        case "$a" in [yY]*) ;; *) echo "已取消"; exit 0 ;; esac
        CMDS=(-c "init" -c "halt"
              -c "flash erase_sector 0 0 last"
              -c "reset" -c "exit") ;;
    reset)
        CMDS=(-c "init" -c "halt"
              -c "exit") ;;
    *)
        echo "❌ 未知模式: $MODE" >&2; usage >&2; exit 2 ;;
esac

# ---------- 执行 ----------
if ! "$OPENOCD_BIN" -f "$CFG" "${CMDS[@]}"; then
    echo
    echo "❌ 烧录失败。排查顺序：" >&2
    echo "   1) WCH-Link 是否插好、是否被系统识别（lsusb）" >&2
    echo "   2) 目标板是否已上电、GND 是否共地" >&2
    echo "   3) 固件运行中会关闭仿真调试接口（PA0/PA1 让给串口）—— 给目标板断电重上电后立刻重试" >&2
    echo "   4) 报 flash protected 时改用: $0 -m unlock-program <firmware.hex>" >&2
    exit 1
fi

echo
case "$MODE" in
    verify|erase-program|unlock-program)
        echo "✅ 烧录完成（已校验并复位运行）" ;;
    verify-only)
        echo "✅ 校验通过" ;;
    erase-all)
        echo "✅ 已全片擦除" ;;
    reset)
        echo "✅ 已复位并停在 halt（可接 GDB 调试）" ;;
esac
