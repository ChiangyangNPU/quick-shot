#!/usr/bin/env bash
#
# QuickShot → Gitee Release 发布
#
# 两条通路共用本脚本，行为一致：
#   - CI：release.yml 的 `Publish to Gitee` job（tag 构建完成后自动执行，凭仓库密钥
#         GITEE_TOKEN；缺失即报错退出，避免 CI 全绿而 Gitee 缺产物）
#   - 本地：CI 失败/超时后补发，或想立刻发完
#          GITEE_TOKEN=<私人令牌> deploy/gitee/publish_gitee_release.sh
#
# 做的事：按 tag 取 GitHub Release 的**同一份 CI 产物** → 创建/复用 Gitee Release
# （说明取 tag 注释）→ 上传附件（失败重试）→ 以服务端实际下载字节数逐项校验。
#
# 用法：
#   GITEE_TOKEN=<私人令牌> deploy/gitee/publish_gitee_release.sh [tag] [产物目录]
#
#   tag       默认：仓库中最新 tag（git describe --tags --abbrev=0）
#   产物目录  默认：./release-download（脚本会把 GitHub Release 产物下载到此）
#
#   GITEE_TOKEN：Gitee → 设置 → 私人令牌，需 projects 权限
#                （也可用 --token-file <文件> 从文件读取）
#   GH_TOKEN：可选。读 GitHub Release 用；设置后带上令牌可规避匿名 API 限流
#             （CI 里传的是 Actions 自动提供的 GITHUB_TOKEN）
#
# 可重复执行：已存在且下载字节数一致的附件会跳过上传。
#
# @author chiangyang

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
REPO_SLUG="chiangyangNPU/quick-shot"     # GitHub owner/repo（读取产物）
GITEE_SLUG="chiangyangNPU/quick-shot"    # Gitee owner/repo（发布目标）
UA="Mozilla/5.0 (compatible; quickshot-release-bot/1.0)"

TAG=""
ASSET_DIR=""
TOKEN_FILE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --token-file) TOKEN_FILE="$2"; shift 2 ;;
        -*) echo "未知参数: $1" >&2; exit 2 ;;
        *) if [ -z "$TAG" ]; then TAG="$1"; else ASSET_DIR="$1"; fi; shift ;;
    esac
done

if [ -n "$TOKEN_FILE" ]; then
    GITEE_TOKEN="$(cat "$TOKEN_FILE")"
fi
if [ -z "${GITEE_TOKEN:-}" ]; then
    echo "错误：请设置 GITEE_TOKEN 或用 --token-file 指定令牌文件" >&2
    exit 2
fi

[ -n "$TAG" ] || TAG="$(cd "$PROJECT_ROOT" && git describe --tags --abbrev=0)"
ASSET_DIR="${ASSET_DIR:-$PROJECT_ROOT/release-download}"
mkdir -p "$ASSET_DIR"

# GitHub API：有 GH_TOKEN 就带上（匿名调用有速率限制，CI 里必带）
api_github() {
    if [ -n "${GH_TOKEN:-}" ]; then
        curl -s -A "$UA" -H "Authorization: Bearer $GH_TOKEN" "$@"
    else
        curl -s -A "$UA" "$@"
    fi
}
api_gitee() { curl -s -A "$UA" "$@"; }

# 文件字节数（macOS 的 stat -f%z 与 GNU 的 stat -c%s 不通用，用 wc 保持可移植）
file_size() { wc -c < "$1" | tr -d ' \n'; }

# 下载 GitHub Release 资源（$1=url $2=输出文件）
gh_download() {
    if [ -n "${GH_TOKEN:-}" ]; then
        curl -sL -A "$UA" -H "Authorization: Bearer $GH_TOKEN" \
            -H "Accept: application/octet-stream" --max-time 1800 -o "$2" "$1"
    else
        curl -sL -A "$UA" -H "Accept: application/octet-stream" \
            --max-time 1800 -o "$2" "$1"
    fi
}

# 远端附件的实际字节数（Gitee/GitHub API 不返回大小或不可信，直接看下载响应头）
remote_size() {
    curl -sIL -A "$UA" "$1" | tr -d '\r' | awk 'tolower($1)=="content-length:"{n=$2} END{print n+0}'
}

echo "== 发布 $TAG 到 Gitee（产物来自 GitHub Release） =="

# ---------- 1. 取 GitHub Release 的产物清单与说明 ----------
api_github "https://api.github.com/repos/$REPO_SLUG/releases/tags/$TAG" > /tmp/gh_release.json
python3 - "$TAG" <<'PY'
import json, sys
tag = sys.argv[1]
d = json.load(open("/tmp/gh_release.json"))
if d.get("message"):
    sys.exit(f"GitHub Release 不存在: {tag}（{d['message']}）")
if d.get("draft"):
    sys.exit(f"GitHub Release {tag} 仍是草稿，先发布再传 Gitee")
assets = [a for a in d.get("assets", []) if a["name"].startswith("QuickShot-Release-")]
if not assets:
    sys.exit(f"GitHub Release {tag} 没有 QuickShot-Release-* 产物")
with open("/tmp/gh_assets.txt", "w") as f:
    for a in assets:
        f.write(f"{a['id']}\t{a['name']}\t{a['size']}\n")
print(f"GitHub 产物 {len(assets)} 个:")
for a in assets:
    print(f"  - {a['name']} ({a['size']} bytes)")
PY

# ---------- 2. 下载产物（走 api.github.com 资源接口，绕开 github.com 直连限制） ----------
while IFS=$'\t' read -r id name size; do
    target="$ASSET_DIR/$name"
    if [ -f "$target" ] && [ "$(file_size "$target")" = "$size" ]; then
        echo "已存在且大小一致，跳过下载: $name"
        continue
    fi
    echo "下载 $name ..."
    gh_download "https://api.github.com/repos/$REPO_SLUG/releases/assets/$id" "$target"
    actual="$(file_size "$target")"
    if [ "$actual" != "$size" ]; then
        echo "错误：${name} 下载不完整（本地 ${actual} ≠ 远端 ${size}）" >&2
        exit 1
    fi
done < /tmp/gh_assets.txt

# ---------- 3. 发布说明：tag 注释 + Full Changelog（与 GitHub 端同源） ----------
NOTES_FILE="/tmp/quickshot_release_notes_$TAG.md"
{
    (cd "$PROJECT_ROOT" && git tag -l --format='%(contents)' "$TAG") || true
    echo ''
    echo "**Full Changelog**: https://github.com/$REPO_SLUG/commits/$TAG"
} > "$NOTES_FILE"

# ---------- 4. 复用或创建 Gitee Release ----------
json_id() { python3 -c "
import sys, json
try:
    print(json.load(sys.stdin).get('id') or '')
except Exception:
    print('')
"; }

RELEASE_ID=$(api_gitee "https://gitee.com/api/v5/repos/$GITEE_SLUG/releases/tags/$TAG" | json_id)
if [ -n "$RELEASE_ID" ]; then
    echo "复用已有 Gitee Release: id=$RELEASE_ID"
else
    RESP=$(api_gitee -X POST "https://gitee.com/api/v5/repos/$GITEE_SLUG/releases" \
        -F "access_token=$GITEE_TOKEN" \
        -F "tag_name=$TAG" \
        -F "name=$TAG" \
        --form-string "body=$(cat "$NOTES_FILE")" \
        -F "target_commitish=master")
    # 标题用纯 tag 名（vX.Y.Z），与 GitHub Release 默认标题保持一致
    RELEASE_ID=$(printf '%s' "$RESP" | json_id)
    [ -n "$RELEASE_ID" ] || { echo "创建 Gitee Release 失败: $RESP" >&2; exit 1; }
    echo "已创建 Gitee Release: id=$RELEASE_ID"
fi

# ---------- 5. 上传附件（同名同大小视为已传，支持重跑） ----------
api_gitee "https://gitee.com/api/v5/repos/$GITEE_SLUG/releases/$RELEASE_ID" > /tmp/gitee_release.json

while IFS=$'\t' read -r id name size; do
    url="https://gitee.com/$GITEE_SLUG/releases/download/$TAG/$name"
    if python3 -c "
import json,sys
d=json.load(open('/tmp/gitee_release.json'))
sys.exit(0 if any(a.get('name')=='$name' for a in d.get('assets',[])) else 1)"; then
        if [ "$(remote_size "$url")" = "$size" ]; then
            echo "Gitee 已有且大小一致，跳过: $name"
            continue
        fi
    fi
    ok=0
    for attempt in 1 2 3; do
        # 变量一律用 ${} 包裹：后面紧跟中文全角字符时，bash 会把多字节字符
        # 的字节并进变量名解析（macOS 自带 bash 3.2 实测 "name?: unbound variable"）
        echo "上传 ${name}（第 ${attempt} 次）..."
        code=$(curl -s -A "$UA" --speed-limit 512 --speed-time 900 \
            -o "/tmp/gitee_attach_$name.json" -w "%{http_code}" -X POST \
            "https://gitee.com/api/v5/repos/$GITEE_SLUG/releases/$RELEASE_ID/attach_files" \
            -F "access_token=$GITEE_TOKEN" -F "file=@$ASSET_DIR/$name")
        [ "${code:-000}" -ge 200 ] && [ "${code:-000}" -lt 300 ] && { ok=1; break; }
        echo "失败 HTTP ${code:-000}: $(head -c 200 "/tmp/gitee_attach_$name.json")" >&2
        sleep 10
    done
    [ "$ok" -eq 1 ] || { echo "附件上传失败: $name" >&2; exit 1; }
done < /tmp/gh_assets.txt

# ---------- 6. 校验：逐项核对 Gitee 下载地址的实际字节数 ----------
FAILED=0
while IFS=$'\t' read -r id name size; do
    got=$(remote_size "https://gitee.com/$GITEE_SLUG/releases/download/$TAG/$name")
    if [ "$got" = "$size" ]; then
        echo "OK: $name ($got bytes)"
    else
        echo "缺失或大小不符: $name 期望 $size 实际 $got" >&2
        FAILED=1
    fi
done < /tmp/gh_assets.txt
[ "$FAILED" -eq 0 ] || { echo "Gitee 校验未通过" >&2; exit 1; }

echo "Gitee 发布完成：https://gitee.com/$GITEE_SLUG/releases/tag/$TAG"
