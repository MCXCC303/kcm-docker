# Privileges/credentials: core uses the KAuth client API and KWallet (helper runs as root after auth)
find_package(KF6 6.5 REQUIRED COMPONENTS Auth Wallet Config Archive)

# --- Core library ------------------------------------------------------------
# backend + model: linked by the KCM plugin and the unit tests, keeping the
# layers testable (ARCH_V1 §28/§30/§35)
add_library(kcm_docker_core STATIC
    i18n.cpp
    logging.cpp

    backend/build_context.cpp
    backend/build_context.h
    backend/directory_picker.cpp
    backend/directory_picker.h
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
    backend/docker_cli_auth_writer.cpp
    backend/docker_cli_auth_writer.h
    backend/log_frame_reader.cpp
    backend/log_frame_reader.h
    backend/docker_cli_auth_importer.h
    backend/credential_store.h
    backend/kwallet_credential_store.cpp
    backend/kwallet_credential_store.h
    backend/registry_auth.cpp
    backend/service_control.cpp
    backend/service_control.h
    backend/service_status.cpp
    backend/service_status.h
    backend/registry_auth.h
    backend/privileged_config_client.cpp
    backend/privileged_config_client.h

    dto/container_dto.cpp
    dto/container_network_dto.h
    dto/container_dto.h
    dto/container_inspect_dto.cpp
    dto/container_inspect_dto.h
    dto/engine_dto.cpp
    dto/engine_dto.h
    dto/volume_dto.cpp
    dto/volume_dto.h
    dto/network_dto.cpp
    dto/network_dto.h
    dto/image_build_dto.cpp
    dto/image_build_dto.h
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
    domain/container_create_request.cpp
    domain/image_build.h
    domain/container_create_request.h
    domain/volume.cpp
    domain/volume.h
    domain/network.cpp
    domain/network.h
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
    model/network_detail_controller.cpp
    model/network_detail_controller.h
    model/host_port_filter_model.cpp
    model/host_port_filter_model.h
    model/host_port_model.cpp
    model/host_port_model.h
    model/host_port_usage.cpp
    model/host_port_usage.h
    model/port_binding_rules.cpp
    model/port_binding_rules.h
    model/row_sync_plan.cpp
    model/row_sync_plan.h
    model/app_config_path.cpp
    model/app_config_path.h
    model/command_history_store.cpp
    model/command_history_store.h
    model/create_container_controller.cpp
    model/create_container_controller.h
    model/mount_preset_store.cpp
    model/mount_preset_store.h
    model/volume_detail_controller.cpp
    model/volume_detail_controller.h
    model/volume_filter_model.cpp
    model/volume_filter_model.h
    model/volume_model.cpp
    model/volume_model.h
    model/network_filter_model.cpp
    model/network_filter_model.h
    model/network_model.cpp
    model/network_model.h
    model/image_model.cpp
    model/image_model.h
    model/image_build_model.cpp
    model/image_build_model.h
    model/image_pull_model.cpp
    model/image_pull_model.h
    model/metrics_model.cpp
    model/metrics_model.h
    model/mount_list_model.cpp
    model/mount_list_model.h
    model/operation_controller.cpp
    model/operation_controller.h
    model/port_mapping_group_model.cpp
    model/port_mapping_group_model.h
    model/port_mapping_model.cpp
    model/port_mapping_model.h
    model/presentation.cpp
    model/presentation.h
    model/container_log_controller.cpp
    model/container_log_controller.h
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

target_include_directories(kcm_docker_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(kcm_docker_core PUBLIC KCM_DOCKER_BUILD_STAMP="${KCM_DOCKER_BUILD_STAMP}")

target_link_libraries(kcm_docker_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Qml
        # Restricted privilege client: only the KAuth/DBus **client** API; authorization happens on call
        Qt6::DBus
        KF6::AuthCore
        # Credential storage: KWallet is the only persistent location (ARCH_V5_V8 §2.6)
        KF6::Wallet
        # Mount preset persistence (~/.config/kcm_dockerrc, §4.1)
        KF6::ConfigCore
        # Build context packaging (phase 8 §5.2)
        KF6::Archive
        # Directory picking: QFileDialog (native dialog comes from the platform)
        Qt6::Widgets
        KF6::CoreAddons
        KF6::I18n
)

target_compile_definitions(kcm_docker_core PUBLIC KCM_DOCKER_VERSION="${PROJECT_VERSION}")

# The plugin is a MODULE library, so the static lib must be PIC
set_target_properties(kcm_docker_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- KCM plugin --------------------------------------------------------------
# Builds kcm_docker.so into ${KDE_INSTALL_PLUGINDIR}/plasma/kcms/systemsettings/
# (kcmshell6 / systemsettings only look for KCMs under that namespace; the
#  plugin's embedded kcm_docker.json carries the module metadata)
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
        # The KIO implementation goes into the plugin only, never into core:
        # unit tests must not need desktop services just because of KIO.
        # "Open host directory" is the only external action of phase 4 and is
        # confined to this one file (§2.1.1).
        backend/kio_host_path_service.cpp
        backend/kio_host_path_service.h
)

target_link_libraries(kcm_docker PRIVATE
    kcm_docker_core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    KF6::CoreAddons
    KF6::KCMUtilsQuick
    KF6::KIOGui
)

# --- QML UI ------------------------------------------------------------------
# Compiled into the plugin's own qrc; the resource path must be
# :/kcm/<plugin id>/main.qml (KQuickConfigModule::mainUi() loads the UI there).
#
# The file list lives in the top-level CMakeLists.txt (KCM_DOCKER_QML_FILES) and
# is shared with tests/ui/tst_qml_resource.cpp so the two cannot diverge.
qt_add_resources(kcm_docker "kcm_docker_qml"
    PREFIX "/kcm/kcm_docker"
    BASE "${KCM_DOCKER_QML_BASE_DIR}"
    FILES
        ${KCM_DOCKER_QML_FILES}
)

# --- Restricted privilege helper (ARCH_V5_V8 §2.4) ---------------------------
# The only executable that runs as root; PrivilegedConfigRequest limits it to
# whitelisted keys + fixed paths + fixed units. The validation logic is shared
# with core, so "an out-of-scope request is rejected" is testable without root.
add_executable(kcm_docker_helper kauth/kcm_docker_helper.cpp)
target_link_libraries(kcm_docker_helper PRIVATE
    kcm_docker_core
    Qt6::Core
    Qt6::DBus
    KF6::AuthCore
)

# System integration of the privileged pieces: polkit policy, the helper's D-Bus
# service file, D-Bus system policy.
#
# Where the polkit policy comes from (ARCH_V5_V8 appendix C): **the source is
# .actions (INI)**, the .policy (XML) is a build product made by KAuth's
# kauth-policy-gen. Handing XML to kauth_install_actions() does not fail; it
# produces a policy file with no actions, so authorization always fails on the
# packaged system (really happened, do not revert).
set(KCM_DOCKER_ACTIONS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/kauth/org.kde.kcm.docker.actions")
set(KCM_DOCKER_POLICY_FILE "${CMAKE_CURRENT_BINARY_DIR}/org.kde.kcm.docker.policy")

# Note: KAuth's standard macros install to **absolute system paths**
# (/usr/lib/kf6/kauth, /usr/share/polkit-1/actions, /usr/share/dbus-1/...) — that
# is for distribution packaging and needs root. Developer prefix installs
# (cmake --install into ~/kde/usr) would fail outright as a normal user (really
# happened). Hence two paths:
#
#   - default: install only the helper binary into the prefix and **generate** the policy;
#              an explicit script does the system part (needs sudo)
#   - packaging/system install: -DKCM_DOCKER_INSTALL_PRIVILEGED_HELPER=ON uses the KAuth macros
option(KCM_DOCKER_INSTALL_PRIVILEGED_HELPER
       "Install the polkit policy and the helper D-Bus service into system paths (requires root)" OFF)

if(KCM_DOCKER_INSTALL_PRIVILEGED_HELPER)
    # The helper must land in KAUTH_HELPER_INSTALL_DIR: the D-Bus activation file's
    # Exec is <KAUTH_HELPER_INSTALL_DIR>/kcm_docker_helper, so anywhere else
    # (e.g. /usr/bin) gives a package that looks fine yet reports "helper not
    # found" on every escalation (KAuth requires this; PKGBUILD checks all four).
    install(TARGETS kcm_docker_helper RUNTIME DESTINATION ${KAUTH_HELPER_INSTALL_DIR})

    # Official macros: generate/install <helper_id>.conf (D-Bus policy allowing root to own
    # the name), <helper_id>.service (activation), and the polkit .policy from .actions
    kauth_install_helper_files(kcm_docker_helper org.kde.kcm.docker root)
    kauth_install_actions(org.kde.kcm.docker "${KCM_DOCKER_ACTIONS_FILE}")
else()
    # Dev prefix install: helper goes into bin (install-privileged-helper.sh takes it from build/bin).
    install(TARGETS kcm_docker_helper ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

    # Same official tool and input format, just not into system paths (the install
    # script does that as root); without it the script has no policy — hence ALL.
    add_custom_command(OUTPUT "${KCM_DOCKER_POLICY_FILE}"
                       COMMAND KF6::kauth-policy-gen "${KCM_DOCKER_ACTIONS_FILE}" "${KCM_DOCKER_POLICY_FILE}"
                       MAIN_DEPENDENCY "${KCM_DOCKER_ACTIONS_FILE}"
                       COMMENT "Generating org.kde.kcm.docker.policy from org.kde.kcm.docker.actions")
    add_custom_target(kcm_docker_policy ALL DEPENDS "${KCM_DOCKER_POLICY_FILE}")
    message(STATUS "kcm-docker: privileged helper files are not installed by cmake --install; "
                   "run the generated install-privileged-helper.sh with sudo (see README)")
endif()

# Explicit one-step privileged install script, same system paths as the official
# macros, run with sudo. All four files are required (ARCH_V5_V8 appendix C):
# helper binary, polkit policy, D-Bus service file (activation), D-Bus system policy (own bus name).
set(KCM_DOCKER_PRIVILEGED_INSTALL_SCRIPT "${CMAKE_BINARY_DIR}/install-privileged-helper.sh")
# CMake generates install/uninstall: the four paths are defined once, not copied into the README
file(WRITE "${KCM_DOCKER_PRIVILEGED_INSTALL_SCRIPT}"
"#!/bin/sh\n"
"# Generated by CMake (see src/CMakeLists.txt): install the restricted helper and\n"
"# its system integration files, or remove them all with the uninstall argument.\n"
"# All four files are required: helper binary, polkit policy, D-Bus service file\n"
"# (activation), D-Bus system policy (own the bus name).\n"
"# Usage:\n"
"#   install-privileged-helper.sh            install (needs root)\n"
"#   install-privileged-helper.sh uninstall  uninstall (needs root)\n"
"set -e\n"
"KAUTH_DIR=/usr/lib/kf6/kauth\n"
"POLKIT_DIR=/usr/share/polkit-1/actions\n"
"DBUS_SERVICE_DIR=/usr/share/dbus-1/system-services\n"
"DBUS_POLICY_DIR=/usr/share/dbus-1/system.d\n"
"HELPER=\"\$KAUTH_DIR/kcm_docker_helper\"\n"
"POLICY=\"\$POLKIT_DIR/org.kde.kcm.docker.policy\"\n"
"SERVICE=\"\$DBUS_SERVICE_DIR/org.kde.kcm.docker.service\"\n"
"BUSCONF=\"\$DBUS_POLICY_DIR/org.kde.kcm.docker.conf\"\n"
"\n"
"# Old name (the project was once kontainer): cleaned up on install so two sets of\n"
"# privileged components cannot coexist on one system\n"
"LEGACY_HELPER=\"\$KAUTH_DIR/kontainer_helper\"\n"
"LEGACY_POLICY=\"\$POLKIT_DIR/org.kde.kontainer.policy\"\n"
"LEGACY_SERVICE=\"\$DBUS_SERVICE_DIR/org.kde.kontainer.service\"\n"
"LEGACY_BUSCONF=\"\$DBUS_POLICY_DIR/org.kde.kontainer.conf\"\n"
"\n"
"case \"\${1:-install}\" in\n"
"install)\n"
"    rm -f \"\$LEGACY_HELPER\" \"\$LEGACY_POLICY\" \"\$LEGACY_SERVICE\" \"\$LEGACY_BUSCONF\"\n"
"    install -d \"\$KAUTH_DIR\" \"\$POLKIT_DIR\" \"\$DBUS_SERVICE_DIR\" \"\$DBUS_POLICY_DIR\"\n"
"    install -m 0755 \"${CMAKE_BINARY_DIR}/bin/kcm_docker_helper\" \"\$HELPER\"\n"
"    install -m 0644 \"${KCM_DOCKER_POLICY_FILE}\" \"\$POLICY\"\n"
"\n"
"    # D-Bus activation: on the first call the bus starts the helper as root from this file\n"
"    cat > \"\$SERVICE\" <<'EOF'\n"
"[D-BUS Service]\n"
"Name=org.kde.kcm.docker\n"
"Exec=/usr/lib/kf6/kauth/kcm_docker_helper\n"
"User=root\n"
"EOF\n"
"\n"
"    # D-Bus policy: the system bus defaults to <deny own=\"*\"/> + <deny send_type=\"method_call\"/>,\n"
"    # so every service must open its own hole in .conf or the helper cannot even own the bus\n"
"    # name (KAuth's dbus_policy.stub is that hole; we also allow the caller's send_destination)\n"
"    cat > \"\$BUSCONF\" <<'EOF'\n"
"<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
"<busconfig>\n"
"  <policy user=\"root\">\n"
"    <allow own=\"org.kde.kcm.docker\"/>\n"
"  </policy>\n"
"  <policy context=\"default\">\n"
"    <allow send_destination=\"org.kde.kcm.docker\"/>\n"
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
"    # Remove only these four files, no wildcards, so nothing else in /usr is deleted by accident\n"
"    rm -f \"\$HELPER\" \"\$POLICY\" \"\$SERVICE\" \"\$BUSCONF\"\n"
"    rm -f \"\$LEGACY_HELPER\" \"\$LEGACY_POLICY\" \"\$LEGACY_SERVICE\" \"\$LEGACY_BUSCONF\"\n"
"    echo \"removed: \$HELPER\"\n"
"    echo \"removed: \$POLICY\"\n"
"    echo \"removed: \$SERVICE\"\n"
"    echo \"removed: \$BUSCONF\"\n"
"    echo \"note: polkit reloads its action directory automatically; run 'sudo systemctl reload dbus' for the D-Bus policy to take effect\"\n"
"    ;;\n"
"*)\n"
"    echo \"usage: \$0 [install|uninstall]\" >&2\n"
"    exit 2\n"
"    ;;\n"
"esac\n"
)
message(STATUS "kcm-docker: write privileged install script: ${KCM_DOCKER_PRIVILEGED_INSTALL_SCRIPT}")
