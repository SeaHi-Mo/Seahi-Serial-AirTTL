#!/usr/bin/env bash
#
# SeaHi-Serial-AirTTL —— 串口 ISP 烧录脚本（WCHISPTool_CMD）
#
# 用途：走 CH570Q 片内 ROM bootloader（ISP）烧录，**不依赖 WCH-Link / 两线调试口**。
#       两个固件运行后都会关掉 PA0/PA1 的仿真调试功能（从机是把这两个脚让给 UART，
#       `R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN`），导致 WCH-Link / OpenOCD 极难连上；
#       ISP 这条路完全不碰调试口，所以更可靠。
#
# ★ 时序要点（关键）：
#   CH570 的 ROM 在**上电瞬间**检测 ISP 握手数据 —— 也就是"先让工具开始发数据，
#   再给 MCU 上电"。而 WCHISPTool_CMD 只枚举一次就退出（不会等设备），
#   所以要用 -w 进入等待模式：脚本反复调用工具，你在这期间给 MCU 上电。
#
# 前置条件：
#   ① MCU 需要一次上电/复位（配合 -w 的等待窗口），见 references/flashing.md 的「方法：ISP 烧录」
#   ② 串口在 WSL 里可见（usbipd 映射后形如 /dev/ttyUSB0 / /dev/ttyACM0）
#   ③ 已有 Config.ini —— 必须用 Windows 版 WchIspStudio.exe 的「文件→保存配置」生成
#
# 示例：
#   scripts/isp-flash.sh -c ~/Config.ini -f RF_Uart/build/RF_Uart.hex
#   scripts/isp-flash.sh -c ~/Config.ini -f RF_Uart/build/RF_Uart.hex -o verify
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

TOOL="${ISP_BIN:-}"          # WCHISPTool_CMD 路径
WAIT=0                       # 等待重试秒数（0=只试一次）
PORT="${ISP_PORT:-}"          # 真实串口设备；优先级: -p/--port > 环境变量 ISP_PORT > 自动查找
LINK="/dev/ttyISP0"          # 工具要求的设备名（必须叫 ttyISPx）
BAUD=115200
CFG=""                       # Config.ini
OP="program"                 # program | verify
FW=""                        # 固件

usage() {
    cat <<'EOF'
用法: isp-flash.sh -c <Config.ini> -f <firmware.hex> [选项]

必填:
  -c, --config <path>     Config.ini（Windows 版 WchIspStudio 生成）
  -f, --flash  <path>     固件：xxx.hex 或 xxx.bin

选项:
  -p, --port <dev>        串口设备（默认自动查找 /dev/ttyUSB*、/dev/ttyACM*）
                              也可用环境变量 ISP_PORT 指定（便于 CMake 构建时覆盖）
  -b, --baud <n>          波特率，默认 115200
  -o, --operation <x>     program（下载，默认）| verify（校验）
  -t, --tool <path>       WCHISPTool_CMD 路径（默认自动查找）
  -w, --wait <sec>        等待模式：反复重试直到成功或超时（推荐 10~15）
                          期间请给 MCU 上电 —— BOOT 检测发生在上电瞬间
  -h, --help              显示本帮助

提示:
  · 运行前必须让 MCU 进入 BOOT 下载模式，否则会报「未枚举到设备」(状态码 5)
  · 默认用 sudo 调用工具（工具通常需要 root）；已配 udev/权限充足时可 ISP_SUDO=0
  · 没有现成可用工具时，脚本会打印自行编译的命令
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -c|--config)    CFG="${2:?}";  shift 2 ;;
        -f|--flash)     FW="${2:?}";   shift 2 ;;
        -p|--port)      PORT="${2:?}"; shift 2 ;;
        -b|--baud)      BAUD="${2:?}"; shift 2 ;;
        -o|--operation) OP="${2:?}";   shift 2 ;;
        -t|--tool)      TOOL="${2:?}"; shift 2 ;;
        -w|--wait)      WAIT="${2:?}"; shift 2 ;;
        -h|--help)      usage; exit 0 ;;
        *) echo "未知选项: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "$OP" in program|verify) ;; *) echo "❌ -o 只能是 program 或 verify" >&2; exit 2 ;; esac
[ -n "$CFG" ] || { echo "❌ 必须用 -c 指定 Config.ini" >&2; usage >&2; exit 2; }
[ -f "$CFG" ] || { echo "❌ Config.ini 不存在: $CFG" >&2; exit 1; }
[ -n "$FW" ]  || { echo "❌ 必须用 -f 指定固件" >&2; usage >&2; exit 2; }
[ -f "$FW" ]  || { echo "❌ 固件不存在: $FW" >&2; exit 1; }

# ---------- 找 WCHISPTool_CMD ----------
# 注意：tools/wchisptool/bin 下那份是官方预编译版，要求 glibc >= 2.33；
#       Ubuntu 20.04（glibc 2.31）跑不起来，需自行编译（见 build_tool 提示）。
find_tool() {
    local c
    for c in "$TOOL" \
             "$HOME/.local/bin/WCHISPTool_CMD" \
             "$REPO_ROOT/tools/wchisptool/bin/x64/WCHISPTool_CMD"; do
        if [ -n "$c" ] && [ -x "$c" ]; then echo "$c"; return 0; fi
    done
    return 1
}

build_hint() {
    cat >&2 <<EOF

❌ 没找到可用的 WCHISPTool_CMD。
   官方预编译版（tools/wchisptool/bin/x64/）要求 glibc>=2.33，本机若是
   Ubuntu 20.04（glibc 2.31）会报 "GLIBC_2.33 not found"，请自行编译：

     cd $REPO_ROOT/tools/wchisptool
     g++ src/IspCmdTool.cpp -I lib/x64/dynamic \\
         -o \$HOME/.local/bin/WCHISPTool_CMD \\
         lib/x64/static/libwch55xisp-4.0.0.a -lpthread

   编译产物放到 ~/.local/bin/ 后本脚本会自动找到，也可用 -t 指定路径。
EOF
}

TOOL="$(find_tool)" || { build_hint; exit 1; }

# 校验工具能不能跑（glibc 版本不符时会在这一步暴露）。
# 注意两点：
#   ① 本工具的 -h 返回码是 1（不是 0），所以只能看输出内容，不能看返回码；
#   ② 脚本开了 pipefail，而 -h 的非 0 退出码会让整条管道判失败，
#      因此这里先捕获输出（|| true），再单独 grep。
TOOL_PROBE="$("$TOOL" -h 2>&1 || true)"
if ! printf '%s' "$TOOL_PROBE" | grep -q "TOOL VERSION"; then
    echo "❌ 找到的 WCHISPTool_CMD 无法执行（多半是 glibc 版本不符）：$TOOL" >&2
    printf '%s\n' "$TOOL_PROBE" | head -3 >&2
    build_hint
    exit 1
fi

# ---------- 找串口 ----------
if [ -z "$PORT" ]; then
    mapfile -t cands < <(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true)
    if [ "${#cands[@]}" -eq 0 ]; then
        echo "❌ 没找到串口（/dev/ttyUSB* 或 /dev/ttyACM*）" >&2
        echo "   若设备在 Windows 侧，先用 usbipd 映射进 WSL；" >&2
        echo "   映射后可用 lsusb 确认，必要时 dmesg | tail 看 tty 节点名。" >&2
        exit 1
    elif [ "${#cands[@]}" -gt 1 ]; then
        echo "⚠️  检测到多个串口，请用 -p 指定：" >&2
        printf '     %s\n' "${cands[@]}" >&2
        exit 1
    fi
    PORT="${cands[0]}"
fi

[ -e "$PORT" ] || { echo "❌ 串口设备不存在: $PORT" >&2; exit 1; }

# ---------- 决定是否需要 sudo ----------
# 软链已正确存在、且目标串口可写时无需提权（例如节点已是 666，或本用户在 dialout 组）。
# 可用 ISP_SUDO=1 强制 sudo、ISP_SUDO=0 强制不提权。
# ⚠️ 默认就用 sudo：WCHISPTool 通常需要 root。
# 不要用 `[ -w "$PORT" ]` 判断! —— WSL 的 usbip 串口设备权限位会骗人：只读能打开、
# 以读写方式打开被拒，而 test -w 却返回真。之前据此跳过 sudo，导致工具打不开串口，
# 一路报 `Code:7 Fail to get device info`（手动带 sudo 却能成功）。
# 若确无需要提权（例如已配 udev MODE=0666），可设 ISP_SUDO=0。
case "${ISP_SUDO:-1}" in
    0)  SUDO="" ;;
    *)  SUDO=sudo ;;
esac

# ---------- 建立工具要求的 ttyISPx 软链接 ----------
if [ "$PORT" != "$LINK" ]; then
    if [ -L "$LINK" ] && [ "$(readlink -f "$LINK")" = "$(readlink -f "$PORT")" ]; then
        :   # 已是正确软链，无需重建
    else
        echo "▶ 创建软链接: $LINK -> $PORT"
        $SUDO ln -sfn "$PORT" "$LINK"
    fi
fi

echo "▶ 工具    : $TOOL"
echo "▶ 配置    : $CFG"
echo "▶ 固件    : $FW"
echo "▶ 操作    : $OP"
echo "▶ 串口    : $LINK -> $PORT  @$BAUD"
echo "▶ 提权    : ${SUDO:-（不用 sudo）}"
echo

# ---------- 执行 ----------
# 工具的"未枚举到设备"= 状态码 5。BOOT 检测发生在上电瞬间，
# 所以等待模式下反复重试，让用户有机会给 MCU 上电。
run_once() {
    set +e
    OUT=$($SUDO "$TOOL" -p "$LINK" -b "$BAUD" -c "$CFG" -o "$OP" -f "$FW" 2>&1)
    RC=$?
    set -e
    # 输出先捕获、不直接透传：等待期间逐次透传会把工具头部刷屏。
    # 需要完整输出时设 ISP_VERBOSE=1。
    return "$RC"
}

RC=0
if [ "${WAIT:-0}" -gt 0 ] 2>/dev/null; then
    echo "▶ 等待模式：${WAIT}s 内反复尝试；请现在给 MCU 上电（或复位）"
    echo
    deadline=$(( $(date +%s) + WAIT ))
    t0=$(date +%s)
    attempt=0
    last=""
    while :; do
        attempt=$((attempt + 1))
        run_once || RC=$?
        if [ "$RC" -eq 0 ]; then
            echo "$OUT" | grep -E '"Status"' | sed 's/^/  /'
            echo
            break
        fi
        # 只在错误码变化时打一行，其余用 \r 原地刷新，避免刷屏
        cur=$(printf '%s' "$OUT" | grep -o '"Code":[0-9]*' | tail -1 | grep -o '[0-9]*')
        if [ "$cur" != "$last" ]; then
            [ -n "$last" ] && echo
            printf '  状态码 %s  %s\n' "${cur:-?}" "$(printf '%s' "$OUT" | grep -o '"Message":"[^"]*"' | tail -1)"
            last="$cur"
        fi
        printf '\r  第 %d 次尝试… 已等 %ss/%ss        ' "$attempt" "$(( $(date +%s) - t0 ))" "$WAIT"
        # 5=未枚举到设备、7=串口开了但读不到芯片信息 —— 两者都是"还没进 BOOT"的典型表现
        # （BOOT 只在上电瞬间存在），所以都要在等待窗口内继续重试。
        case "$RC" in
            5|7) ;;
            *)   echo; break ;;
        esac
        if [ "$(date +%s)" -ge "$deadline" ]; then echo; break; fi
        # 工具自身会 "Wait isp dev timeout" 约 2 秒（实测单次运行 2.05s），
        # 无需再 sleep，监听便无空档。
    done
else
    run_once || RC=$?
    echo "$OUT"
fi

echo
[ "${ISP_VERBOSE:-0}" = "1" ] && printf '%s\n' "$OUT"
case "$RC" in
    0)  echo "✅ 操作成功（状态码 0）" ;;
    4)  echo "❌ 串口名称无效（状态码 4）—— 检查 $LINK 软链接" ;;
    5)  echo "❌ 未枚举到设备（状态码 5）—— MCU 很可能不在 BOOT 下载模式，或串口没接对" ;;
    7)  echo "❌ 读不到设备信息（状态码 7）—— 串口已打开但芯片未响应，通常仍是没赶上上电瞬间；权限不足也会报这个码" ;;
    6)  echo "❌ 芯片类型与配置不符（状态码 6）—— Config.ini 里选的型号要对得上 CH570" ;;
    13) echo "❌ 下载失败（状态码 13）" ;;
    14) echo "❌ 校验失败（状态码 14）" ;;
    *)  echo "❌ 失败，状态码 $RC（对照使用说明的 1.3 状态码表）" ;;
esac
exit "$RC"
