cmake_minimum_required(VERSION 3.20)

project(kontainer
    VERSION 0.5.0
    DESCRIPTION "Docker/容器的 KDE 系统设置模块 (KCM)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- ECM ---------------------------------------------------------------------
# KDEInstallDirs  : 计算 KDE_INSTALL_* 安装目录，并生成 build/prefix.sh
# KDECMakeSettings: 输出目录、rpath 等 KDE 项目默认设置
find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)

# --- Qt6 / KF6 ---------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(KF6 6.5 REQUIRED COMPONENTS CoreAddons Config I18n KCMUtils)
# KIO 只被 KCM 插件使用（容器详情「打开宿主目录」），core 与单元测试都不依赖它
find_package(KF6 6.5 REQUIRED COMPONENTS KIO)

qt_standard_project_setup()

# --- 警告 ---------------------------------------------------------------------
# 项目要求 -Wall -Wextra 下 0 警告（README 的验收项）。
# 写进 CMakeLists 而不是靠命令行传参：否则换台机器、换个人构建时会静默失去这层检查，
# 而它正是三期用来发现「未使用变量 / 结构体成员」这类问题的手段。
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# --- 构建标记 -----------------------------------------------------------------
# 曾经出现「用户装的插件到底是哪一次构建」无法判断的情况：安装与重新链接只差
# 0.3 秒，事后只能靠 md5 猜。因此把 git 短哈希与构建时间编进二进制并打印出来，
# 之后任何一次崩溃都能立刻确定对应哪份代码。
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE KONTAINER_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT KONTAINER_GIT_HASH)
    set(KONTAINER_GIT_HASH "unknown")
endif()
string(TIMESTAMP KONTAINER_BUILD_TIME "%Y-%m-%d %H:%M" UTC)
set(KONTAINER_BUILD_STAMP "${PROJECT_VERSION}+${KONTAINER_GIT_HASH} (${KONTAINER_BUILD_TIME} UTC)")

# 翻译：po/<lang>/<domain>.po 会被编译并安装到 ${KDE_INSTALL_LOCALEDIR}
# （KLocalizedString 从 XDG_DATA_DIRS 下的 locale/ 目录读取，prefix.sh 已包含本前缀）
ki18n_install(po)

include(CTest)

# --- 界面文件清单 -------------------------------------------------------------
# 插件 qrc 与「从 qrc 加载界面」的测试共用这一份清单（BASE 都是 src/ui）。
#
# 为什么不各自维护一份：漏加一个文件时，源码目录下的测试仍然通过，
# 只有安装后的插件会失败——这是一类只在打包后才暴露的问题
# （ARCH_V3 附录 A 记录过同样的陷阱）。
set(KONTAINER_QML_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
# 使用绝对路径：qt_add_resources 的别名按 BASE 计算，不受调用目录影响，
# 因此 src/ 与 tests/ 可以共用同一份清单。
set(KONTAINER_QML_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MainPage.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ContainerCard.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ImageCard.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ContainerDetail.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ImageDetail.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/EngineStatusView.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/DaemonConfigPage.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/RegistryAuthPage.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/NetworkCard.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/NetworkDetail.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/VolumeCard.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/VolumeDetail.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/StorageView.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ResourceView.qml"
    # QML 单例（qmldir 里声明的两个色板）也必须在这里列出，
    # 否则源码目录测试全绿、安装后的插件却打不开。
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/StatusPalette.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/ChartPalette.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/StatTile.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/MiniTrend.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/StorageBar.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/ConfirmDialog.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/OperationMessage.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/PullImageDialog.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/PullProgressList.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/FieldChip.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/PortTopology.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/ImageRefInput.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/StringListEditor.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/RegistryLoginDialog.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/LogConsole.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/CreateNetworkDialog.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/KeyValueListEditor.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/StatusChip.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/CopyButton.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/CopyableText.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/EmptyPlaceholder.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/CollapsibleSection.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/KeyValueList.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/qmldir"
)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
