#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 kontainer developers
# SPDX-License-Identifier: GPL-2.0-or-later
#
# 离屏渲染包装脚本：准备 KDE 配色方案，再调用 kontainer_render_ui。
#
# 用法：
#     tests/tools/render_ui.sh <page> <width> <height> <light|dark> <output.png>
#     page = main | container-detail | image-detail | network-detail | engine | daemon-config | daemon-config-user | registry-auth
#
# 另外两个可选环境变量（只影响截图内容，不影响产品行为）：
#     KONTAINER_RENDER_PULLS=1       在镜像标签页造出「拉取中 + 拉取失败」两条记录
#     KONTAINER_RENDER_MANY_PORTS=1  在容器详情造出 24 条端口映射
#     KONTAINER_RENDER_ROOTLESS=1    让 fixture 报告 rootless 部署（配置页据此走用户配置路径）
#     KONTAINER_RENDER_LANG=zh_CN    用 po/zh_CN 的译文渲染界面（只有 QML 文案会变中文：
#                                    C++ 组装的文本走 ki18n，渲染工具喂不进去）
#     KONTAINER_RENDER_LONG_PATHS=1  挂载路径换成极长的那种（复核省略与靠右对齐）
#     HOME=<临时目录>                让 daemon-config 页读到该目录下的 daemon.json
#                                    （用于复核"用户可写"形态；默认读真实系统配置）
#
# 为什么需要包装脚本（而不是在 C++ 里 qputenv）：
# KColorScheme / KConfig 在进程启动早期就确定了配置位置，实测在 main() 里
# 设置 XDG_CONFIG_HOME 不会生效（会继续读用户真实配置），因此必须在
# 启动进程之前由 shell 提供。
#
# 配色方案取自系统自带的 /usr/share/color-schemes/<scheme>.colors：
# 把整份 .colors 复制成私有 XDG_CONFIG_HOME 下的 kdeglobals——
# 只写 `[General] ColorScheme=` 是不够的，KColorScheme 真正读的是 `[Colors:*]` 段。

set -euo pipefail

if [ "$#" -lt 5 ]; then
    echo "usage: $0 <page> <width> <height> <light|dark> <output.png>" >&2
    exit 2
fi

theme="$4"
case "$theme" in
light) scheme="BreezeLight" ;;
dark) scheme="BreezeDark" ;;
*)
    echo "unknown theme: $theme (expected light or dark)" >&2
    exit 2
    ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$(cd "$script_dir/../../build" && pwd)"
binary="$build_dir/bin/kontainer_render_ui"

if [ ! -x "$binary" ]; then
    echo "missing $binary — build it first: cmake --build build --target kontainer_render_ui" >&2
    exit 1
fi

scheme_file="/usr/share/color-schemes/$scheme.colors"
if [ ! -f "$scheme_file" ]; then
    echo "missing $scheme_file" >&2
    exit 1
fi

config_dir="$build_dir/render-cfg-$scheme"
mkdir -p "$config_dir"
cp "$scheme_file" "$config_dir/kdeglobals"
printf '\n[General]\nColorScheme=%s\n' "$scheme" >> "$config_dir/kdeglobals"

# KDE 的 QQC2 样式（真实会话里的 kcmshell6 同样是它）：
# 默认样式的 Label 颜色来自 QPalette，会让 Kirigami.AbstractCard 内部出现
# 「深色卡片 + 黑色文字」这种只属于离屏渲染的组合。
export XDG_CONFIG_HOME="$config_dir"
export QT_QUICK_CONTROLS_STYLE=org.kde.desktop
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

exec "$binary" "$@"
