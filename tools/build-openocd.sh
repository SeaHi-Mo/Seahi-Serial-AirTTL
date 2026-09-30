#!/usr/bin/env bash
#
# 编译项目自带的 WCH 定制版 OpenOCD（tools/openocd 子模块）
#   用途：给 CH570Q 烧录 / 调试（WCH-Link 或 WCH-LinkE，SDI 接口）
#   产物：tools/openocd/src/openocd
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/tools/openocd"
JOBS="${JOBS:-$(nproc)}"
CLEAN=0

usage() {
    cat <<EOF
用法: $(basename "$0") [-j N] [--clean]

编译 WCH 定制版 OpenOCD（含 wlinke 驱动，支持 WCH-Link + SDI）。
产物：tools/openocd/src/openocd

选项:
  -j N       并行编译数（默认 CPU 核数）
  --clean    先 make clean 再编译
  -h, --help 显示本帮助

编译依赖:
  Debian/Ubuntu: sudo apt install build-essential autoconf automake libtool pkg-config libusb-1.0-0-dev
  Fedora/RHEL:   sudo dnf install gcc make autoconf automake libtool pkgconf-pkg-config libusb1-devel

编译完成后，可直接用本 skill 的烧录脚本（会自动优先使用这个 openocd）：
  skills/coder-ch570q-airttl/scripts/flash.sh RF_Uart/build/RF_Uart.hex
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -j)      JOBS="${2:?}"; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "未知参数: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# ---------- 1) 子模块是否就绪 ----------
if [ ! -f "$SRC/configure.ac" ]; then
    echo "❌ 找不到 tools/openocd 源码，子模块可能未初始化：" >&2
    echo "   git submodule update --init --recursive tools/openocd" >&2
    exit 1
fi

# ---------- 2) 依赖检查 ----------
missing=()
for c in autoconf automake libtoolize make gcc pkg-config; do
    command -v "$c" >/dev/null 2>&1 || missing+=("$c")
done
pkg-config --exists libusb-1.0 2>/dev/null || missing+=("libusb-1.0-0-dev")

if [ ${#missing[@]} -gt 0 ]; then
    echo "❌ 缺少编译依赖: ${missing[*]}" >&2
    echo "   Debian/Ubuntu: sudo apt install build-essential autoconf automake libtool pkg-config libusb-1.0-0-dev" >&2
    echo "   Fedora/RHEL:   sudo dnf install gcc make autoconf automake libtool pkgconf-pkg-config libusb1-devel" >&2
    exit 1
fi

cd "$SRC"

# ---------- 3) 生成 configure（仓库不含，属生成物） ----------
if [ ! -f configure ]; then
    echo "▶ 生成 configure（./bootstrap）..."
    # 注意：bootstrap 末尾会 cd 进内嵌的 libjaylink（J-Link 支持）跑 autogen.sh，
    # 那个步骤在本仓库常常失败；我们只用 WCH-Link（wlinke 驱动），
    # 因此容忍该失败，只要主项目的 configure 能生成出来就继续。
    ./bootstrap || echo "⚠️  bootstrap 有报错（多为内嵌 libjaylink 的 autogen 失败，不影响 WCH-Link）"
fi
if [ ! -f configure ]; then
    echo "❌ bootstrap 未能生成 configure，请确认 autoconf/automake/libtool 已装好" >&2
    exit 1
fi

# ---------- 4) configure ----------
echo "▶ configure（启用 wlinke 驱动）..."
# shellcheck disable=SC2086
./configure --enable-wlinke --disable-internal-libjaylink --disable-werror ${EXTRA_CONFIGURE_ARGS:-}

# ---------- 5) 编译 ----------
[ "$CLEAN" = "1" ] && make clean
echo "▶ 编译（-j$JOBS）..."
make -j"$JOBS"

echo
echo "✅ 编译完成"
echo "   openocd  : $SRC/src/openocd"
echo "   tcl 脚本 : $SRC/tcl/（含 target/wch-riscv.cfg）"
"$SRC/src/openocd" --version 2>/dev/null | head -1 || true
echo
echo "烧录用法见 skills/coder-ch570q-airttl/references/flashing.md"
