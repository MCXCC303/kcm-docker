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
