# 提权与凭据：core 直接用到 KAuth 客户端 API 与 KWallet（helper 只在授权后以 root 执行）
find_package(KF6 6.5 REQUIRED COMPONENTS Auth Wallet)

# --- 核心库 -------------------------------------------------------------------
# backend + model：与 KCM 插件、单元测试共同链接，保证分层可测试（ARCH_V1 §28/§30/§35）
add_library(kontainer_core STATIC
    i18n.cpp
    logging.cpp

    backend/docker_api_version.cpp
    backend/docker_api_version.h
    backend/daemon_config.cpp
    backend/daemon_config.h
    backend/daemon_deployment.cpp
    backend/daemon_deployment.h
    backend/docker_api_paths.h
    backend/docker_backend.cpp
    backend/docker_backend.h
    backend/docker_backend_interface.cpp
    backend/docker_backend_interface.h
    backend/docker_capabilities.cpp
    backend/docker_capabilities.h
    backend/docker_client.cpp
    backend/docker_client.h
    backend/docker_endpoint.cpp
    backend/docker_endpoint.h
    backend/docker_error.cpp
    backend/docker_error.h
    backend/http/http_response_parser.cpp
    backend/http/http_response_parser.h
    backend/host_path_service.cpp
    backend/host_path_service.h
    backend/http/json_line_reader.cpp
    backend/http/json_line_reader.h
    backend/privileged_client.h
    backend/credential_store.cpp
    backend/docker_cli_auth_importer.cpp
    backend/log_frame_reader.cpp
    backend/log_frame_reader.h
    backend/docker_cli_auth_importer.h
    backend/credential_store.h
    backend/kwallet_credential_store.cpp
    backend/kwallet_credential_store.h
    backend/registry_auth.cpp
    backend/registry_auth.h
    backend/privileged_config_client.cpp
    backend/privileged_config_client.h

    dto/container_dto.cpp
    dto/container_dto.h
    dto/container_inspect_dto.cpp
    dto/container_inspect_dto.h
    dto/engine_dto.cpp
    dto/engine_dto.h
    dto/image_dto.cpp
    dto/image_dto.h
    dto/image_inspect_dto.cpp
    dto/image_inspect_dto.h
    dto/image_pull_dto.cpp
    dto/image_pull_dto.h
    dto/json_helpers.h
    dto/stats_dto.cpp
    dto/stats_dto.h
    dto/storage_dto.cpp
    dto/storage_dto.h

    domain/container.cpp
    domain/container.h
    domain/container_detail.h
    domain/container_stats.cpp
    domain/container_stats.h
    domain/engine_info.h
    domain/image.cpp
    domain/image.h
    domain/image_detail.cpp
    domain/image_detail.h
    domain/image_pull_progress.h
    domain/image_reference.cpp
    domain/image_reference.h
    domain/storage_usage.cpp
    domain/storage_usage.h

    model/container_detail_controller.cpp
    model/container_detail_controller.h
    model/container_filter_model.cpp
    model/container_filter_model.h
    model/container_model.cpp
    model/container_model.h
    model/detail_entry.h
    model/daemon_config_controller.cpp
    model/daemon_config_controller.h
    model/detail_list_model.cpp
    model/detail_list_model.h
    model/docker_error_text.cpp
    model/docker_error_text.h
    model/engine_status.cpp
    model/engine_status.h
    model/format.cpp
    model/format.h
    model/image_detail_controller.cpp
    model/image_detail_controller.h
    model/image_filter_model.cpp
    model/image_filter_model.h
    model/image_model.cpp
    model/image_model.h
    model/image_pull_model.cpp
    model/image_pull_model.h
    model/metrics_model.cpp
    model/metrics_model.h
    model/mount_list_model.cpp
    model/mount_list_model.h
    model/operation_controller.cpp
    model/operation_controller.h
    model/port_mapping_model.cpp
    model/port_mapping_model.h
    model/presentation.cpp
    model/presentation.h
    model/registry_auth_controller.cpp
    model/registry_auth_controller.h
    model/registry_credential_model.cpp
    model/registry_credential_model.h
    model/qml_registration.cpp
    model/qml_registration.h
    model/refresh_scheduler.cpp
    model/refresh_scheduler.h
    model/state_text.cpp
    model/state_text.h
    model/status_controller.cpp
    model/status_controller.h
    model/storage_status.cpp
    model/storage_status.h

    kauth/privileged_config_request.cpp
    kauth/privileged_config_request.h

    refresh_policy.h
)

target_include_directories(kontainer_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(kontainer_core PUBLIC KONTAINER_BUILD_STAMP="${KONTAINER_BUILD_STAMP}")

target_link_libraries(kontainer_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Qml
        # 受限提权客户端：只有 KAuth/DBus 的**客户端** API，执行时才会发起授权
        Qt6::DBus
        KF6::AuthCore
        # 凭据存储：KWallet 是唯一持久化位置（ARCH_V5_V8 §2.6）
        KF6::Wallet
        KF6::CoreAddons
        KF6::I18n
)

target_compile_definitions(kontainer_core PUBLIC KONTAINER_VERSION="${PROJECT_VERSION}")

# 插件是 MODULE 库，静态库必须是 PIC
set_target_properties(kontainer_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- KCM 插件 -----------------------------------------------------------------
# 生成 kcm_docker.so，安装到 ${KDE_INSTALL_PLUGINDIR}/plasma/kcms/systemsettings/
# （kcmshell6 / systemsettings 只在这个命名空间下查找 KCM，
#  插件内嵌的 kcm_docker.json 提供模块元数据）
set(CMAKE_AUTOMOC_MACRO_NAMES
    ${CMAKE_AUTOMOC_MACRO_NAMES}
    K_PLUGIN_FACTORY_WITH_JSON
    K_PLUGIN_CLASS_WITH_JSON
)

kcoreaddons_add_plugin(kcm_docker
    INSTALL_NAMESPACE "plasma/kcms/systemsettings"
    SOURCES
        kcm/docker_kcm.cpp
        kcm/docker_kcm.h
        kcm/kcm_docker.json
        # KIO 实现只进插件（不进 core）：单元测试不应该因为链接 KIO 而需要桌面服务。
        # 「打开宿主目录」是四期唯一的外部动作，且被限制在这一个文件里（§2.1.1）。
        backend/kio_host_path_service.cpp
        backend/kio_host_path_service.h
)

target_link_libraries(kcm_docker PRIVATE
    kontainer_core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    KF6::CoreAddons
    KF6::KCMUtilsQuick
    KF6::KIOGui
)

# --- QML 界面 -----------------------------------------------------------------
# 打进插件自己的 qrc，资源路径必须是 :/kcm/<插件id>/main.qml
# （KQuickConfigModule::mainUi() 就从这个位置加载界面）
#
# 文件清单定义在顶层 CMakeLists.txt（KONTAINER_QML_FILES），
# 与 tests/ui/tst_qml_resource.cpp 共用，避免两处清单不一致。
qt_add_resources(kcm_docker "kcm_docker_qml"
    PREFIX "/kcm/kcm_docker"
    BASE "${KONTAINER_QML_BASE_DIR}"
    FILES
        ${KONTAINER_QML_FILES}
)

# --- 受限提权 helper（ARCH_V5_V8 §2.4） ----------------------------------------
# 只有这一个可执行文件以 root 运行；它的能力被 PrivilegedConfigRequest 限制在
# 白名单键 + 固定路径 + 固定单元。校验逻辑与 core 共用，因此可以在无 root 的
# 单元测试里验证"越权请求会被拒绝"。
add_executable(kontainer_helper kauth/kontainer_helper.cpp)
target_link_libraries(kontainer_helper PRIVATE
    kontainer_core
    Qt6::Core
    Qt6::DBus
    KF6::AuthCore
)
install(TARGETS kontainer_helper ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# 提权组件的系统集成：polkit 策略、helper 的 D-Bus 服务文件、D-Bus 系统策略。
#
# 关于 polkit 策略的来源（ARCH_V5_V8 附录 C）：**源头是 .actions（INI）**，
# .policy（XML）是构建产物，由 KAuth 自带的 kauth-policy-gen 生成。
# 把 XML 直接交给 kauth_install_actions() 不会报错，而是生成一个"有文件、没有动作"
# 的空策略——打包出来的系统上授权永远失败（真实踩过，别改回去）。
set(KONTAINER_ACTIONS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/kauth/org.kde.kontainer.actions")
set(KONTAINER_POLICY_FILE "${CMAKE_CURRENT_BINARY_DIR}/org.kde.kontainer.policy")

# 注意：KAuth 的标准宏把文件装到**系统绝对路径**（/usr/lib/kf6/kauth、
# /usr/share/polkit-1/actions、/usr/share/dbus-1/...）——那是给发行版打包用的，需要 root。
# 开发者常用的前缀安装（cmake --install 装到 ~/kde/usr）如果也走这些规则，
# 就会在普通用户下直接失败（真实踩过）。因此拆成两条路：
#
#   - 默认：只把 helper 二进制装进前缀，另外**生成**策略；系统部分由一个显式脚本完成（需要 sudo）
#   - 打包/系统安装：-DKONTAINER_INSTALL_PRIVILEGED_HELPER=ON，走 KAuth 标准宏（含 D-Bus 策略）
option(KONTAINER_INSTALL_PRIVILEGED_HELPER
       "Install the polkit policy and the helper D-Bus service into system paths (requires root)" OFF)

if(KONTAINER_INSTALL_PRIVILEGED_HELPER)
    # 官方宏：生成并安装 <helper_id>.conf（D-Bus 系统策略：放行 root own 该名字）、
    # <helper_id>.service（D-Bus 激活）、以及由 .actions 生成的 polkit .policy
    kauth_install_helper_files(kontainer_helper org.kde.kontainer root)
    kauth_install_actions(org.kde.kontainer "${KONTAINER_ACTIONS_FILE}")
else()
    # 同一套官方工具与输入格式，只是不往系统路径装（由 install-privileged-helper.sh 提权安装）。
    # 少了这一步，下面的脚本就没有策略文件可装——所以生成目标是 ALL。
    add_custom_command(OUTPUT "${KONTAINER_POLICY_FILE}"
                       COMMAND KF6::kauth-policy-gen "${KONTAINER_ACTIONS_FILE}" "${KONTAINER_POLICY_FILE}"
                       MAIN_DEPENDENCY "${KONTAINER_ACTIONS_FILE}"
                       COMMENT "Generating org.kde.kontainer.policy from org.kde.kontainer.actions")
    add_custom_target(kontainer_policy ALL DEPENDS "${KONTAINER_POLICY_FILE}")
    message(STATUS "Kontainer: privileged helper files are not installed by cmake --install; "
                   "run the generated install-privileged-helper.sh with sudo (see README)")
endif()

# 显式的一步提权安装脚本：路径与官方宏使用的系统路径一致，用 sudo 执行即可。
# 四个文件缺一不可（ARCH_V5_V8 附录 C）：
#   helper 二进制、polkit 策略、D-Bus 服务文件（激活）、D-Bus 系统策略（own 总线名）
set(KONTAINER_PRIVILEGED_INSTALL_SCRIPT "${CMAKE_BINARY_DIR}/install-privileged-helper.sh")
# 安装/卸载脚本由 CMake 生成：四个文件的路径只有一份定义，避免 README 里再抄一遍
file(WRITE "${KONTAINER_PRIVILEGED_INSTALL_SCRIPT}"
"#!/bin/sh\n"
"# 由 CMake 生成（见 src/CMakeLists.txt）：把受限 helper 及其系统集成文件\n"
"# 安装到系统路径，或用 uninstall 参数把它们删干净。四个文件缺一不可：\n"
"#   helper 二进制、polkit 策略、D-Bus 服务文件（激活）、D-Bus 系统策略（own 总线名）\n"
"# 用法：\n"
"#   install-privileged-helper.sh            安装（需要 root）\n"
"#   install-privileged-helper.sh uninstall  卸载（需要 root）\n"
"set -e\n"
"KAUTH_DIR=/usr/lib/kf6/kauth\n"
"POLKIT_DIR=/usr/share/polkit-1/actions\n"
"DBUS_SERVICE_DIR=/usr/share/dbus-1/system-services\n"
"DBUS_POLICY_DIR=/usr/share/dbus-1/system.d\n"
"HELPER=\"\$KAUTH_DIR/kontainer_helper\"\n"
"POLICY=\"\$POLKIT_DIR/org.kde.kontainer.policy\"\n"
"SERVICE=\"\$DBUS_SERVICE_DIR/org.kde.kontainer.service\"\n"
"BUSCONF=\"\$DBUS_POLICY_DIR/org.kde.kontainer.conf\"\n"
"\n"
"case \"\${1:-install}\" in\n"
"install)\n"
"    install -d \"\$KAUTH_DIR\" \"\$POLKIT_DIR\" \"\$DBUS_SERVICE_DIR\" \"\$DBUS_POLICY_DIR\"\n"
"    install -m 0755 \"${CMAKE_BINARY_DIR}/bin/kontainer_helper\" \"\$HELPER\"\n"
"    install -m 0644 \"${KONTAINER_POLICY_FILE}\" \"\$POLICY\"\n"
"\n"
"    # D-Bus 激活：总线在第一次调用时按这个文件以 root 启动 helper\n"
"    cat > \"\$SERVICE\" <<'EOF'\n"
"[D-BUS Service]\n"
"Name=org.kde.kontainer\n"
"Exec=/usr/lib/kf6/kauth/kontainer_helper\n"
"User=root\n"
"EOF\n"
"\n"
"    # D-Bus 系统策略：系统总线默认 <deny own=\"*\"/> + <deny send_type=\"method_call\"/>，\n"
"    # 每个服务必须在自己 .conf 里打洞，否则 helper 连总线名都 own 不了\n"
"    # （KAuth 的 dbus_policy.stub 就是这个洞；这里额外放行调用方 send_destination）\n"
"    cat > \"\$BUSCONF\" <<'EOF'\n"
"<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
"<busconfig>\n"
"  <policy user=\"root\">\n"
"    <allow own=\"org.kde.kontainer\"/>\n"
"  </policy>\n"
"  <policy context=\"default\">\n"
"    <allow send_destination=\"org.kde.kontainer\"/>\n"
"  </policy>\n"
"</busconfig>\n"
"EOF\n"
"\n"
"    echo \"installed: \$HELPER\"\n"
"    echo \"installed: \$POLICY\"\n"
"    echo \"installed: \$SERVICE\"\n"
"    echo \"installed: \$BUSCONF\"\n"
"    ;;\n"
"uninstall)\n"
"    # 只删这四个我们自己装的文件；不用通配符，避免误删系统目录里的别的东西\n"
"    rm -f \"\$HELPER\" \"\$POLICY\" \"\$SERVICE\" \"\$BUSCONF\"\n"
"    echo \"removed: \$HELPER\"\n"
"    echo \"removed: \$POLICY\"\n"
"    echo \"removed: \$SERVICE\"\n"
"    echo \"removed: \$BUSCONF\"\n"
"    echo \"note: polkit 会自动重载动作目录；D-Bus 策略要生效请执行 sudo systemctl reload dbus\"\n"
"    ;;\n"
"*)\n"
"    echo \"usage: \$0 [install|uninstall]\" >&2\n"
"    exit 2\n"
"    ;;\n"
"esac\n"
)
message(STATUS "Kontainer: write privileged install script: ${KONTAINER_PRIVILEGED_INSTALL_SCRIPT}")
