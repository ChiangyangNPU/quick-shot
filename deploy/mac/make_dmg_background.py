#!/usr/bin/env python3
"""生成 mac DMG 引导背景图（QuickShot 标题 + 版本号 + 拖动提示 + 安装箭头）

用法: python3 make_dmg_background.py <版本号> <输出路径>
依赖 Pillow；未安装时 deploy_mac.sh 自动回退到仓库内静态背景 dmg-background.png
（无版本号文本）。版本号布局与图标坐标（deploy_mac.sh 中 AppleScript）联动：
app 与 Applications 图标中心约定在 (170, 250) / (490, 250)，箭头画在同一水平线。

@author chiangyang
"""
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 660, 400


def find_font(size, prefer_cjk=False):
    """按用途挑选系统字体：CJK 文本用苹方，避免西文字体渲染中文出现方框"""
    candidates = (
        ["/System/Library/Fonts/PingFang.ttc",
         "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"]
        if prefer_cjk else
        ["/System/Library/Fonts/SFNS.ttf",
         "/System/Library/Fonts/Helvetica.ttc"]
    )
    for p in candidates:
        try:
            return ImageFont.truetype(p, size)
        except Exception:
            continue
    return ImageFont.load_default()


def center_text(d, y, text, font, fill):
    bbox = d.textbbox((0, 0), text, font=font)
    w = bbox[2] - bbox[0]
    d.text(((W - w) / 2 - bbox[0], y), text, font=font, fill=fill)


def main(version, out):
    img = Image.new("RGB", (W, H), (246, 246, 248))
    d = ImageDraw.Draw(img)
    center_text(d, 50, "QuickShot", find_font(60), (29, 29, 31))
    center_text(d, 134, f"v{version}", find_font(26), (134, 134, 139))
    center_text(d, 184, "拖动 QuickShot 到右侧 Applications 完成安装",
                find_font(20, prefer_cjk=True), (156, 156, 161))
    # 安装箭头：与 AppleScript 的图标位置联动，app (170,270) → Applications (490,270)
    # （Finder 定位的图标视觉中心比坐标低约 30px，坐标 240 → 视觉中心 ~270）
    y, x0, x1 = 270, 245, 415
    d.rectangle([x0, y - 9, x1 - 38, y + 9], fill=(196, 196, 202))
    d.polygon([(x1 - 44, y - 34), (x1, y), (x1 - 44, y + 34)], fill=(196, 196, 202))
    img.save(out)
    print("background written:", out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
