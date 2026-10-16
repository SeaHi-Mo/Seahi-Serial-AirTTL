#!/usr/bin/env bash
#
# 把 GitHub Release 里已经构建好的固件，同步到 Gitee 的发行版（Release）。
#
# 为什么不在 Gitee 侧重新编译：代码是从 GitHub 镜像过来的，同一份源码编出来的东西
# 必然一样，而重新编译要拖 2.2 GB 的 tools/toolchain 子模块、吃 Gitee Go 的构建额度。
# 固件总共才 ~180 KB，直接搬运划算得多。
#
# 由 Gitee Go 流水线调用，见 .workflow/sync-release.yml。
#
# 环境变量（前缀特意避开 GITEE_ / GO_ —— 那是 Gitee Go 的系统保留字，
#           GITEE_REPO 更是内置参数，会把同名自定义变量覆盖成 `owner/repo`）：
#   SYNC_TOKEN      必填。Gitee 私人令牌（Gitee → 设置 → 私人令牌，勾选 projects 权限）
#   SYNC_OWNER      必填。Gitee 用户名／组织名，例：seahi007
#   SYNC_REPO       必填。Gitee 仓库名，例：Seahi-Serial-AirTTL
#   GH_REPO         可选。GitHub 仓库，默认 SeaHi-Mo/Seahi-Serial-AirTTL
#   TAG             可选。只同步指定版本（例 v1.0.0）；默认取 GitHub 的最新 Release
#   GITHUB_TOKEN    可选。带上可避开 GitHub API 的匿名限额（60 次/小时/IP）
#
set -euo pipefail

GH_REPO="${GH_REPO:-SeaHi-Mo/Seahi-Serial-AirTTL}"

: "${SYNC_TOKEN:?缺少 SYNC_TOKEN（Gitee 私人令牌）}"
: "${SYNC_OWNER:?缺少 SYNC_OWNER}"
: "${SYNC_REPO:?缺少 SYNC_REPO}"

GITEE_API="https://gitee.com/api/v5/repos/${SYNC_OWNER}/${SYNC_REPO}"

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

fail() {
    echo "" >&2
    echo "错误：$*" >&2
    # 有响应体就一并打出来，Gitee 的报错信息都在 message 字段里
    if [ -s "${TMP}/resp.json" ]; then
        echo "  服务端返回：$(head -c 500 "${TMP}/resp.json")" >&2
    fi
    exit 1
}

# ---------------------------------------------------------------------------
# 0. 依赖：curl 必需（multipart 传附件），jq 用来解析 JSON
# ---------------------------------------------------------------------------
for bin in curl jq; do
    if ! command -v "${bin}" >/dev/null 2>&1; then
        echo "== 当前环境没有 ${bin}，尝试安装 =="
        if command -v apt-get >/dev/null 2>&1; then
            apt-get install -y -qq "${bin}" || { apt-get update -qq && apt-get install -y -qq "${bin}"; }
        fi
    fi
    command -v "${bin}" >/dev/null 2>&1 || fail "缺少 ${bin}，装不上就没法继续"
done

# ---------------------------------------------------------------------------
# 1. 从 GitHub 取要同步的那个 Release
# ---------------------------------------------------------------------------
if [ -n "${TAG:-}" ]; then
    GH_URL="https://api.github.com/repos/${GH_REPO}/releases/tags/${TAG}"
    echo "== 目标版本：${TAG}（指定）"
else
    GH_URL="https://api.github.com/repos/${GH_REPO}/releases/latest"
    echo "== 目标版本：GitHub 最新 Release"
fi

gh_curl() {
    if [ -n "${GITHUB_TOKEN:-}" ]; then
        curl -sSL --max-time 60 -H "Authorization: Bearer ${GITHUB_TOKEN}" \
             -H "Accept: application/vnd.github+json" "$@"
    else
        curl -sSL --max-time 60 -H "Accept: application/vnd.github+json" "$@"
    fi
}

GH_CODE="$(gh_curl -o "${TMP}/gh.json" -w '%{http_code}' "${GH_URL}")" || GH_CODE=000
[ "${GH_CODE}" = "200" ] || {
    echo "  服务端返回：$(head -c 500 "${TMP}/gh.json" 2>/dev/null)" >&2
    fail "取 GitHub Release 失败（HTTP ${GH_CODE}）—— Gitee 构建机可能访问不了 api.github.com"
}

TAG="$(jq -r '.tag_name // empty' "${TMP}/gh.json")"
[ -n "${TAG}" ] || fail "GitHub 返回里没有 tag_name"
RELEASE_NAME="$(jq -r '.name // empty' "${TMP}/gh.json")"
[ -n "${RELEASE_NAME}" ] || RELEASE_NAME="${TAG}"
BODY="$(jq -r '.body // ""' "${TMP}/gh.json")"
TARGET_COMMITISH="$(jq -r '.target_commitish // "master"' "${TMP}/gh.json")"

echo "  版本号：${TAG}（${RELEASE_NAME}）"

mapfile -t ASSET_URLS < <(jq -r '.assets[]?.browser_download_url' "${TMP}/gh.json")
mapfile -t ASSET_NAMES < <(jq -r '.assets[]?.name' "${TMP}/gh.json")
[ "${#ASSET_URLS[@]}" -gt 0 ] || fail "Release ${TAG} 里一个附件都没有，没什么可同步的"

echo "  待同步附件（${#ASSET_URLS[@]} 个）："
for n in "${ASSET_NAMES[@]}"; do echo "    - ${n}"; done

# ---------------------------------------------------------------------------
# 2. Gitee 侧：找同名发行版，没有就建
# ---------------------------------------------------------------------------
gitee_call() {   # 用法同 curl；把响应体写进 $TMP/resp.json，HTTP 码从 stdout 出来
    local code
    code="$(curl -sS --max-time 300 -o "${TMP}/resp.json" -w '%{http_code}' "$@" 2>"${TMP}/curlerr")" || code=000
    [ -n "${code}" ] || code=000
    printf '%s' "${code}"
}

echo ""
echo "== 查 Gitee 上的发行版 =="
CODE="$(gitee_call -G "${GITEE_API}/releases/tags/${TAG}" \
        --data-urlencode "access_token=${SYNC_TOKEN}")"

case "${CODE}" in
    200)
        RELEASE_ID="$(jq -r '.id // empty' "${TMP}/resp.json")"
        [ -n "${RELEASE_ID}" ] || fail "Gitee 返回里没有 release id"
        echo "  已有 ${TAG} 的发行版（id=${RELEASE_ID}），复用并覆盖附件"
        ;;
    404)
        echo "  还没有 ${TAG} 的发行版，创建"
        CODE="$(gitee_call -X POST "${GITEE_API}/releases" \
                --data-urlencode "access_token=${SYNC_TOKEN}" \
                --data-urlencode "tag_name=${TAG}" \
                --data-urlencode "name=${RELEASE_NAME}" \
                --data-urlencode "body=${BODY}" \
                --data-urlencode "target_commitish=${TARGET_COMMITISH}")"
        [ "${CODE:0:1}" = "2" ] || fail "创建 Gitee 发行版失败（HTTP ${CODE}）—— 若提示 tag 不存在，先把仓库同步到 Gitee 再来"
        RELEASE_ID="$(jq -r '.id // empty' "${TMP}/resp.json")"
        [ -n "${RELEASE_ID}" ] || fail "创建成功但返回里没有 release id"
        echo "  创建成功（id=${RELEASE_ID}）"
        ;;
    401|403)
        fail "Gitee 拒绝了这个令牌（HTTP ${CODE}）—— 检查 SYNC_TOKEN 是否有效、是否勾了 projects 权限"
        ;;
    *)
        fail "查 Gitee 发行版失败（HTTP ${CODE}）"
        ;;
esac

# ---------------------------------------------------------------------------
# 3. 传附件
#    重跑时同名附件会让上传失败或变成重复，所以先列出已有附件、同名的先删掉。
# ---------------------------------------------------------------------------
echo ""
echo "== 列出 Gitee 上已有的附件 =="
CODE="$(gitee_call -G "${GITEE_API}/releases/${RELEASE_ID}/attach_files" \
        --data-urlencode "access_token=${SYNC_TOKEN}" \
        --data-urlencode "per_page=100")"
[ "${CODE:0:1}" = "2" ] || fail "列附件失败（HTTP ${CODE}）"
EXISTING="$(jq -r '.[]? | "\(.name)\t\(.id)"' "${TMP}/resp.json" 2>/dev/null || true)"
[ -n "${EXISTING}" ] && echo "${EXISTING}" | sed 's/^/  /' || echo "  （空）"

echo ""
echo "== 下载并上传 =="
for i in "${!ASSET_URLS[@]}"; do
    url="${ASSET_URLS[$i]}"
    name="${ASSET_NAMES[$i]}"

    # 同名的先删掉，保证这一步幂等（重跑不会失败、也不会留两份）
    old_id="$(printf '%s\n' "${EXISTING}" | awk -F'\t' -v n="${name}" '$1==n{print $2; exit}')"
    if [ -n "${old_id}" ]; then
        echo "  [${name}] Gitee 上已有同名附件（id=${old_id}），先删除"
        CODE="$(gitee_call -X DELETE \
                "${GITEE_API}/releases/${RELEASE_ID}/attach_files/${old_id}?access_token=${SYNC_TOKEN}")"
        [ "${CODE:0:1}" = "2" ] || fail "删除旧附件 ${name} 失败（HTTP ${CODE}）"
    fi

    echo "  [${name}] 从 GitHub 下载"
    curl -sSL --max-time 300 -o "${TMP}/${name}" "${url}" \
        || fail "下载 ${name} 失败（Gitee 构建机可能访问不了 GitHub）"
    [ -s "${TMP}/${name}" ] || fail "下载下来的 ${name} 是空文件"

    echo "  [${name}] 上传到 Gitee（$(wc -c < "${TMP}/${name}") 字节）"
    CODE="$(gitee_call -X POST "${GITEE_API}/releases/${RELEASE_ID}/attach_files" \
            -F "access_token=${SYNC_TOKEN}" \
            -F "file=@${TMP}/${name};type=application/octet-stream")"
    [ "${CODE:0:1}" = "2" ] || fail "上传 ${name} 失败（HTTP ${CODE}）"
    echo "    → $(jq -r '.browser_download_url // "(无下载地址)"' "${TMP}/resp.json")"
done

echo ""
echo "== 同步完成 =="
echo "  发行版页面：https://gitee.com/${SYNC_OWNER}/${SYNC_REPO}/releases/tag/${TAG}"
