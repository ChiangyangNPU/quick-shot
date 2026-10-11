#!/bin/bash
# QuickShot mac 本地更新测试：生成 release-API 形状的 version.json + 拷贝
# 更新包 + 启动本地 HTTP 服务器（与 update-test/server 的 Windows PowerShell
# 流程对齐，服务 UpdateManager 的 QUICKSHOT_UPDATE_URL 测试通道）。
#
# 用法:
#   ./prepare-and-serve.sh <DMG路径> [新版本号] [端口]
# 示例:
#   ./prepare-and-serve.sh ../QuickShot-Release-v0.1.0.dmg 0.1.1 8000
#
# version.json 使用 GitHub/Gitee release API 形状（tag_name + assets），
# 使「按平台挑选附件」逻辑在第一渠道即被真实验证。
# 测试应用启动方式:
#   QUICKSHOT_UPDATE_URL=http://127.0.0.1:<端口>/version.json \
#     /Applications/QuickShot.app/Contents/MacOS/QuickShot
# 结束: Ctrl+C 停止服务器。

set -e

DMG="$1"
VERSION="${2:-0.1.1}"
PORT="${3:-8000}"

if [ -z "$DMG" ] || [ ! -f "$DMG" ]; then
    echo "用法: $0 <DMG路径> [新版本号] [端口]"
    echo "示例: $0 ../QuickShot-Release-v0.1.0.dmg 0.1.1 8000"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$SCRIPT_DIR/server-root"
mkdir -p "$SERVER_DIR"

# 更新包按新版本号命名（与发布产物命名约定一致）
cp "$DMG" "$SERVER_DIR/QuickShot-Release-v${VERSION}.dmg"
SIZE=$(stat -f%z "$SERVER_DIR/QuickShot-Release-v${VERSION}.dmg")

# release-API 形状的版本清单（tag_name + assets），QUICKSHOT_UPDATE_URL
# 覆盖后首个渠道（中国大陆为 Gitee 位置）即按本形状解析
cat > "$SERVER_DIR/version.json" <<EOF2
{
    "tag_name": "v${VERSION}",
    "name": "QuickShot v${VERSION} (本地更新测试)",
    "body": "本地模拟更新测试包\\n1. 验证 mac 应用内下载 DMG\\n2. 验证 quarantine 清理与引导安装",
    "assets": [
        {
            "name": "QuickShot-Release-v${VERSION}.dmg",
            "browser_download_url": "http://127.0.0.1:${PORT}/QuickShot-Release-v${VERSION}.dmg",
            "size": ${SIZE}
        }
    ]
}
EOF2

echo "============================================================"
echo " 测试服务器目录: $SERVER_DIR"
echo " 更新包: QuickShot-Release-v${VERSION}.dmg ($SIZE bytes)"
echo " 测试通道: QUICKSHOT_UPDATE_URL=http://127.0.0.1:${PORT}/version.json"
echo " 按 Ctrl+C 停止服务器"
echo "============================================================"

cd "$SERVER_DIR"
exec python3 -m http.server "$PORT" --bind 127.0.0.1
