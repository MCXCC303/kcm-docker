#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 kcm-docker developers
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Offscreen render wrapper: prepares a KDE color scheme, then calls kcm_docker_render_ui.
#
# Usage:
#     tests/tools/render_ui.sh <page> <width> <height> <light|dark> <output.png>
#     page = main | container-detail | image-detail | network-detail | engine | daemon-config | daemon-config-user | registry-auth
#
# Optional environment variables (they only change screenshot content, not product behavior):
#     KCM_DOCKER_RENDER_PULLS=1       image tab gets one "pulling" and one "pull failed" record
#     KCM_DOCKER_RENDER_MANY_PORTS=1  container detail gets 24 port mappings
#     KCM_DOCKER_RENDER_ROOTLESS=1    fixture reports a rootless deployment (config page uses user paths)
#     KCM_DOCKER_RENDER_LANG=zh_CN    render with po/zh_CN translations (only QML strings turn Chinese;
#                                    C++ text goes through ki18n, which the render tool cannot feed)
#     KCM_DOCKER_RENDER_LONG_PATHS=1  extremely long mount paths (checks elision and right alignment)
#     HOME=<temp dir>                 makes the daemon-config page read daemon.json there
#                                    (for the "user-writable" shape; default reads the real system config)
#
# Why a wrapper instead of qputenv in C++:
# KColorScheme / KConfig fix the config location very early during startup; setting
# XDG_CONFIG_HOME inside main() provably does not take effect (the real user config keeps being
# read), so the shell must provide it before the process starts.
#
# The color scheme comes from the system's /usr/share/color-schemes/<scheme>.colors: the whole
# .colors file is copied to kdeglobals in a private XDG_CONFIG_HOME — writing only
# `[General] ColorScheme=` is not enough, KColorScheme actually reads the `[Colors:*]` groups.

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
binary="$build_dir/bin/kcm_docker_render_ui"

if [ ! -x "$binary" ]; then
    echo "missing $binary — build it first: cmake --build build --target kcm_docker_render_ui" >&2
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

# KDE's QQC2 style (what kcmshell6 uses in real sessions): under the default style Label colors
# come from QPalette, which renders Kirigami.AbstractCard as "dark card + black text", an
# offscreen-only combination.
export XDG_CONFIG_HOME="$config_dir"
export QT_QUICK_CONTROLS_STYLE=org.kde.desktop
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

exec "$binary" "$@"
