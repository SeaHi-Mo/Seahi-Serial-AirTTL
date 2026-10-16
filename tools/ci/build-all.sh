#!/usr/bin/env bash
#
# 编译本仓库的全部固件，并把 hex 收集到 dist/ 目录：
#   RF_Uart.hex                  从机（接被调试设备）
#   RF_UartDongle.hex            主机（插电脑 USB）
#   RF_TEST_2474M_ch36.hex       射频测试固件，定频 2474 MHz（信道号 36）
#   RF_TEST_2476M_ch37.hex       射频测试固件，定频 2476 MHz（信道号 37）
#   RF_TEST_2478M_ch38.hex       射频测试固件，定频 2478 MHz（信道号 38）
#
# 三个频点对应应用频段 CH_HOP_TBL = {74,76,78}（见 RF_TEST/APP/include/rf_test.h）。
#
# 本地：./tools/ci/build-all.sh
# CI  ：Gitee Go 走 .workflow/build.yml 调用本脚本；GitHub Actions 走 release.yml（自带同样的步骤）。
#
# 环境变量（都可省略）：
#   TOOLCHAIN_URL  工具链子模块的下载地址。留空则用 .gitmodules 里的地址（GitHub）。
#                  Gitee 构建机拉 GitHub 慢或不通时，指向 Gitee 上的镜像仓库即可。
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${REPO_ROOT}"

JOBS="$(nproc 2>/dev/null || echo 4)"
DIST_DIR="${REPO_ROOT}/dist"

# ---------------------------------------------------------------------------
# 1. 工具链：必须是沁恒定制的 riscv-wch-elf GCC 12.2.0
#    普通 riscv64-unknown-elf-gcc 编不过 —— -march 里的 xw 扩展（mcpy 等自定义
#    指令）只有这个版本支持。
# ---------------------------------------------------------------------------
TOOLCHAIN_GCC="${REPO_ROOT}/tools/toolchain/bin/riscv-wch-elf-gcc"

if [ ! -x "${TOOLCHAIN_GCC}" ]; then
    echo "== tools/toolchain 还没就绪，开始拉取 =="
    if [ -n "${TOOLCHAIN_URL:-}" ]; then
        echo "   来源：TOOLCHAIN_URL=${TOOLCHAIN_URL}"
        rm -rf "${REPO_ROOT}/tools/toolchain"
        mkdir -p "${REPO_ROOT}/tools/toolchain"
        git clone --depth 1 "${TOOLCHAIN_URL}" "${REPO_ROOT}/tools/toolchain"
    else
        echo "   来源：.gitmodules（未设置 TOOLCHAIN_URL）"
        echo "   注意：子模块体积约 2.2 GB，从 GitHub 拉取可能很慢；"
        echo "         Gitee 流水线里可把 TOOLCHAIN_URL 指向国内镜像以加速。"
        git submodule update --init --recursive
    fi
fi

if [ ! -x "${TOOLCHAIN_GCC}" ]; then
    echo "错误：工具链不可用（${TOOLCHAIN_GCC} 不存在或不可执行）" >&2
    echo "  处置：git submodule update --init --recursive，或在流水线里设置 TOOLCHAIN_URL" >&2
    exit 1
fi
echo "== 工具链 =="
"${TOOLCHAIN_GCC}" --version | head -1

# ---------------------------------------------------------------------------
# 2. cmake（部分构建镜像不带，缺了就装）
# ---------------------------------------------------------------------------
if ! command -v cmake >/dev/null 2>&1; then
    echo "== 当前环境没有 cmake，尝试安装 =="
    if command -v apt-get >/dev/null 2>&1; then
        # 先直接装，失败（通常是索引过期）再 update 后重试，省一次 apt update 的等待
        apt-get install -y -qq cmake || { apt-get update -qq && apt-get install -y -qq cmake; }
    elif command -v pip3 >/dev/null 2>&1; then
        pip3 install --quiet cmake
    fi
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "错误：拿不到 cmake（本工程要求 >= 3.16）" >&2
    exit 1
fi
echo "== cmake =="
cmake --version | head -1

# ---------------------------------------------------------------------------
# 3. 编译
#    -B/-S 用仓库根目录下的相对路径，构建产物落在各工程自己的 build 目录里
#    （.gitignore 已忽略），日志留在仓库根目录，方便 CI 出问题时直接看。
# ---------------------------------------------------------------------------
build_one() {
    local name="$1" src="$2" build_dir="$3"
    shift 3
    echo ""
    echo "== 编译 ${name} =="
    cmake -B "${build_dir}" -S "${src}" -G "Unix Makefiles" "$@"
    # 2>&1 | tee：既进日志文件，也进 CI 实时输出（pipefail 保证编译失败会被捕获）
    cmake --build "${build_dir}" -j"${JOBS}" 2>&1 | tee "${name}_build.log"
}

build_one RF_Uart       RF_Uart       RF_Uart/build
build_one RF_UartDongle RF_UartDongle RF_UartDongle/build

# RF_TEST 每个频点单独配置一次（-DRFTEST_DEF_CHANNEL 是配置期变量，不能共用构建目录）
for CH in 36 37 38; do
    case "${CH}" in
        36) FREQ=2474 ;;
        37) FREQ=2476 ;;
        38) FREQ=2478 ;;
    esac
    build_one "RF_TEST_ch${CH}" RF_TEST "RF_TEST/build_${CH}" -DRFTEST_DEF_CHANNEL="${CH}"
done

# ---------------------------------------------------------------------------
# 4. 收集产物 + 自检（缺任何一个就当场失败，别让空文件混进发布）
# ---------------------------------------------------------------------------
echo ""
echo "== 收集固件到 dist/ =="
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"
cp RF_Uart/build/RF_Uart.hex                 "${DIST_DIR}/RF_Uart.hex"
cp RF_UartDongle/build/RF_UartDongle.hex     "${DIST_DIR}/RF_UartDongle.hex"
cp RF_TEST/build_36/RF_TEST.hex              "${DIST_DIR}/RF_TEST_2474M_ch36.hex"
cp RF_TEST/build_37/RF_TEST.hex              "${DIST_DIR}/RF_TEST_2476M_ch37.hex"
cp RF_TEST/build_38/RF_TEST.hex              "${DIST_DIR}/RF_TEST_2478M_ch38.hex"

MISSING=0
for f in "${DIST_DIR}"/*.hex; do
    if [ ! -s "${f}" ]; then
        echo "错误：产物为空 ${f}" >&2
        MISSING=1
    fi
done
[ "${MISSING}" -eq 0 ] || exit 1
ls -l "${DIST_DIR}"

# ---------------------------------------------------------------------------
# 5. 资源占用（从链接日志里抠出 memory region 汇总 / RAM）
# ---------------------------------------------------------------------------
echo ""
echo "== 资源占用 =="
for LOG in RF_Uart_build.log RF_UartDongle_build.log RF_TEST_ch37_build.log; do
    echo ""
    echo "# ${LOG}"
    sed -n '/Memory region/,/RAM:/p' "${LOG}" | head -3 || true
done

echo ""
echo "构建完成。"
