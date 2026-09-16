cmake_minimum_required(VERSION 3.20)

project(kontainer
    VERSION 0.1.0
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

qt_standard_project_setup()

# 翻译：po/<lang>/<domain>.po 会被编译并安装到 ${KDE_INSTALL_LOCALEDIR}
# （KLocalizedString 从 XDG_DATA_DIRS 下的 locale/ 目录读取，prefix.sh 已包含本前缀）
ki18n_install(po)

include(CTest)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
