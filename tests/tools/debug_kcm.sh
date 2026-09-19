#!/bin/sh
# SPDX-FileCopyrightText: 2026 kontainer developers
# SPDX-License-Identifier: GPL-2.0-or-later
#
# 调试用启动器（见 DEBUGGING.md）：把 KCM 在**系统设置**或**独立窗口**里跑起来，
# 并把日志、调试开关、崩溃取证都准备好。
#
# 用法：
#   tests/tools/debug_kcm.sh                      # 在系统设置里打开本模块
#   tests/tools/debug_kcm.sh standalone           # 用 kcmshell6 独立窗口打开
#   tests/tools/debug_kcm.sh settings --gdb       # 在 gdb 里跑系统设置（断点/回溯）
#   tests/tools/debug_kcm.sh standalone --isolated # 用临时 HOME（不碰真实 ~/.config/kontainerrc）
#   tests/tools/debug_kcm.sh --coredumps          # 看最近的崩溃（coredumpctl）
#
# 关键点：插件可以直接从**构建目录**加载（`QT_PLUGIN_PATH=build/bin`），
# 因此改完代码只要 `cmake --build build`，不必每次 `cmake --install`。
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

# 仓库根（本脚本在 tests/tools/ 下）
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build"
PREFIX="${KONTAINER_PREFIX:-$HOME/kde/usr}"

if [ ! -f "$BUILD/bin/plasma/kcms/systemsettings/kcm_docker.so" ]; then
    echo "找不到构建产物，请先：cmake --build $BUILD" >&2
    exit 1
fi

# ① 让宿主找到插件：构建目录本身就是一个合法的插件根
QT_PLUGIN_PATH="$BUILD/bin${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QT_PLUGIN_PATH
# ② 翻译与桌面集成（已装的资源；缺了也只是没有中文，不影响调试）
XDG_DATA_DIRS="$PREFIX/share:/usr/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export XDG_DATA_DIRS
# ③ 我们的日志类别（ARCH §5.17）：默认就开；这里只是确保不被会话配置关掉
QT_LOGGING_RULES="kontainer.*=true;${QT_LOGGING_RULES:-}"
export QT_LOGGING_RULES

if [ "$ACTION" = "--verbose-qml" ]; then
    # QML 绑定被覆盖、属性被重设之类的"静默"问题只有打开这些才看得见
    QT_LOGGING_RULES="qt.qml.binding.removal.info=true;qt.qml.connections=true;kontainer.*=true;$QT_LOGGING_RULES"
    export QT_LOGGING_RULES
fi
if [ "$ACTION" = "--fatal-warnings" ]; then
    # 把第一条 Qt 警告变成崩溃点：定位"到底哪一步先出错"很有效
    QT_FATAL_WARNINGS=1
    export QT_FATAL_WARNINGS
fi

if [ "$ISOLATED" = "1" ]; then
    # 隔离 HOME：KCM 会读写 ~/.config/kontainerrc（挂载预设、命令历史），
    # 调试时不该动真实配置
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

# 系统设置里打开本模块（用法：systemsettings [选项] module）
exec systemsettings kcm_docker
