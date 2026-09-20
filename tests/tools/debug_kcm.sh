#!/bin/sh
# SPDX-FileCopyrightText: 2026 kcm-docker developers
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Debug launcher (see DEBUGGING.md): runs the KCM inside **System Settings** or **standalone**,
# with logging, debug switches and crash forensics ready.
#
# Usage:
#   tests/tools/debug_kcm.sh                      # open the module in System Settings
#   tests/tools/debug_kcm.sh standalone           # open it in a kcmshell6 window
#   tests/tools/debug_kcm.sh settings --gdb       # run System Settings under gdb (breakpoints/backtrace)
#   tests/tools/debug_kcm.sh standalone --isolated # temp HOME (leaves real ~/.config/kcm_dockerrc alone)
#   tests/tools/debug_kcm.sh --coredumps          # inspect the latest crash (coredumpctl)
#
# Key point: the plugin loads straight from the **build dir** (`QT_PLUGIN_PATH=build/bin`), so
# after editing code a `cmake --build build` is enough — no `cmake --install` round trip.
set -e

MODE=settings
ACTION=
ISOLATED=0
for arg in "$@"; do
    case "$arg" in
    standalone | settings) MODE="$arg" ;;
    --gdb | --coredumps | --verbose-qml | --fatal-warnings) ACTION="$arg" ;;
    --isolated) ISOLATED=1 ;;
    -h | --help)
        sed -n '3,20p' "$0"
        exit 0
        ;;
    *)
        echo "未知参数：$arg（用 --help 看用法）" >&2
        exit 2
        ;;
    esac
done

# Repo root (this script lives in tests/tools/)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build"
PREFIX="${KCM_DOCKER_PREFIX:-$HOME/kde/usr}"

if [ ! -f "$BUILD/bin/plasma/kcms/systemsettings/kcm_docker.so" ]; then
    echo "找不到构建产物，请先：cmake --build $BUILD" >&2
    exit 1
fi

# ① Let the host find the plugin: the build dir is itself a valid plugin root
QT_PLUGIN_PATH="$BUILD/bin${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QT_PLUGIN_PATH
# ② Translations and desktop integration (installed resources; missing only means no Chinese UI)
XDG_DATA_DIRS="$PREFIX/share:/usr/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export XDG_DATA_DIRS
# ③ Our logging categories (ARCH §5.17): on by default; this keeps session config from disabling them
QT_LOGGING_RULES="kontainer.*=true;${QT_LOGGING_RULES:-}"
export QT_LOGGING_RULES

if [ "$ACTION" = "--verbose-qml" ]; then
    # Silent QML problems (overwritten bindings, reset properties) only show up with these on
    QT_LOGGING_RULES="qt.qml.binding.removal.info=true;qt.qml.connections=true;kontainer.*=true;$QT_LOGGING_RULES"
    export QT_LOGGING_RULES
fi
if [ "$ACTION" = "--fatal-warnings" ]; then
    # Turn the first Qt warning into a crash: finds "which step fails first" very effectively
    QT_FATAL_WARNINGS=1
    export QT_FATAL_WARNINGS
fi

if [ "$ISOLATED" = "1" ]; then
    # Isolated HOME: the KCM reads and writes ~/.config/kcm_dockerrc (mount presets, command
    # history), and debugging must not touch the real config
    ISOLATED_HOME=$(mktemp -d)
    HOME="$ISOLATED_HOME"
    export HOME
    trap 'rm -rf "$ISOLATED_HOME"' EXIT INT TERM
    echo "已隔离 HOME=$ISOLATED_HOME"
fi

echo "QT_PLUGIN_PATH=$QT_PLUGIN_PATH"
echo "QT_LOGGING_RULES=$QT_LOGGING_RULES"
echo "模式=$MODE（日志直接打在终端；Ctrl+C 结束）"

case "$ACTION" in
--coredumps)
    echo "== 最近的崩溃 =="
    coredumpctl list | tail -10
    echo
    echo "用 'coredumpctl gdb <PID>' 载入；本项目的插件会显示为 kcm_docker.so 的帧"
    exit 0
    ;;
--gdb)
    echo "== 在 gdb 里运行（启动后：run，崩了用 bt）=="
    exec gdb -q -ex run -ex bt --args systemsettings kcm_docker
    ;;
esac

if [ "$MODE" = "standalone" ]; then
    exec kcmshell6 kcm_docker
fi

# Open the module in System Settings (usage: systemsettings [options] module)
exec systemsettings kcm_docker
