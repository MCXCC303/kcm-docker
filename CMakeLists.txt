cmake_minimum_required(VERSION 3.20)

project(kcm-docker
    VERSION 0.9.0
    DESCRIPTION "Docker/容器的 KDE 系统设置模块 (KCM)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- ECM ---------------------------------------------------------------------
# KDEInstallDirs  : computes KDE_INSTALL_* dirs and generates build/prefix.sh
# KDECMakeSettings: output dirs, rpath and other KDE project defaults
find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)

# --- Qt6 / KF6 ---------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(KF6 6.5 REQUIRED COMPONENTS CoreAddons Config I18n KCMUtils)
# KIO is used only by the KCM plugin (container detail "open host directory"); core and tests skip it
find_package(KF6 6.5 REQUIRED COMPONENTS KIO)

qt_standard_project_setup()

# --- Warnings ----------------------------------------------------------------
# Project requires 0 warnings under -Wall -Wextra (README acceptance item).
# Kept in CMakeLists instead of command-line flags: on another machine or by
# another developer the check would silently vanish, and it is what caught
# unused variables / struct members during phase 3.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# --- Build stamp -------------------------------------------------------------
# Once it was impossible to tell which build a user had installed: install and
# relink were 0.3 s apart and only md5 guessing was left. So the git short hash
# and build time are compiled in and printed, pinning any crash to a revision.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE KCM_DOCKER_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT KCM_DOCKER_GIT_HASH)
    set(KCM_DOCKER_GIT_HASH "unknown")
endif()
string(TIMESTAMP KCM_DOCKER_BUILD_TIME "%Y-%m-%d %H:%M" UTC)
set(KCM_DOCKER_BUILD_STAMP "${PROJECT_VERSION}+${KCM_DOCKER_GIT_HASH} (${KCM_DOCKER_BUILD_TIME} UTC)")

# Translations: po/<lang>/<domain>.po is compiled into ${KDE_INSTALL_LOCALEDIR}
# (KLocalizedString reads locale/ below XDG_DATA_DIRS; prefix.sh adds this prefix)
ki18n_install(po)

include(CTest)

# --- UI file list ------------------------------------------------------------
# Shared by the plugin qrc and the "load the UI from qrc" test (BASE is src/ui).
#
# Why one list: a file missing here still passes the source-dir tests and only
# breaks the installed plugin (ARCH_V3 appendix A records that trap).
set(KCM_DOCKER_QML_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
# Absolute paths: qt_add_resources aliases are computed from BASE, not from the
# calling dir, so src/ and tests/ can share this one list.
set(KCM_DOCKER_QML_FILES
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/CreateContainer.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/StorageView.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ResourceView.qml"
    # The QML singletons declared in qmldir (two palettes) must be listed here
    # too, or source-dir tests pass while the installed plugin fails to load.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/StatusPalette.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/ChartPalette.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/StatTile.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/MiniTrend.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/StorageBar.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/ConfirmDialog.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/OperationMessage.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/PullImageDialog.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/PullProgressList.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/BuildImagePanel.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/PortMappingEditor.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/MountPresetManager.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/ServiceCard.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/FilteredComboBox.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/FieldChip.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/PortTopology.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/ImageRefInput.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/StringListEditor.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/RegistryLoginDialog.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/LogConsole.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/CreateNetworkDialog.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/KeyValueListEditor.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/HostPortList.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/HostPortRangeMap.qml"
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
