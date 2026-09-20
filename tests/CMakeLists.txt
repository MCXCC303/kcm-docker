# Unit and integration tests (ARCH_V1 §28/§29/§30)
find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# Test mock backend: keeps model / controller tests independent of a real Docker daemon
add_library(kontainer_test_support STATIC
    support/fake_host_path_service.cpp
    support/fake_host_path_service.h
    support/fake_privileged_client.h
    support/fake_credential_backend.h
    support/mock_docker_backend.cpp
    support/mock_docker_backend.h
    support/qml_stub_kcm.cpp
    support/qml_stub_kcm.h
)
target_include_directories(kontainer_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(kontainer_test_support PUBLIC kcm_docker_core Qt6::Test Qt6::Qml)

function(kcm_docker_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE kcm_docker_core kontainer_test_support Qt6::Test Qt6::Quick)
    # QML loading tests need the source directory
    target_compile_definitions(${name} PRIVATE KCM_DOCKER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    # No test needs X11/Wayland.
    #
    # Language is **pinned to English** as well: cases assert the English strings
    # in the sources, but dev machines often run LANG=zh_CN with this package
    # installed (/usr/share/locale holds kcm_docker.mo), so i18n() returns Chinese
    # and the cases fail en masse — an environment dependency, not a real break.
    # tst_i18n_consistency needs Chinese and overrides this below.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en_US")
endfunction()

kcm_docker_add_test(tst_http_response_parser backend/tst_http_response_parser.cpp)
kcm_docker_add_test(tst_container_inspect_declared_ports dto/tst_container_inspect_declared_ports.cpp)
kcm_docker_add_test(tst_json_line_reader backend/tst_json_line_reader.cpp)
kcm_docker_add_test(tst_log_frame_reader backend/tst_log_frame_reader.cpp)
kcm_docker_add_test(tst_http_request_body backend/tst_http_request_body.cpp)
kcm_docker_add_test(tst_build_context backend/tst_build_context.cpp)
kcm_docker_add_test(tst_service_control backend/tst_service_control.cpp)
kcm_docker_add_test(tst_directory_picker backend/tst_directory_picker.cpp)
kcm_docker_add_test(tst_image_build model/tst_image_build.cpp)
kcm_docker_add_test(tst_daemon_config backend/tst_daemon_config.cpp)
kcm_docker_add_test(tst_daemon_deployment backend/tst_daemon_deployment.cpp)
kcm_docker_add_test(tst_kcm_docker_helper backend/tst_kcm_docker_helper.cpp)
kcm_docker_add_test(tst_kauth_wiring backend/tst_kauth_wiring.cpp)
# The wiring test reads generated artifacts (policy, install script): build dir path + generator dep
target_compile_definitions(tst_kauth_wiring PRIVATE
    KCM_DOCKER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    KCM_DOCKER_BUILD_DIR="${CMAKE_BINARY_DIR}")
if(TARGET kcm_docker_policy)
    add_dependencies(tst_kauth_wiring kcm_docker_policy)
endif()
kcm_docker_add_test(tst_docker_capabilities backend/tst_docker_capabilities.cpp)
kcm_docker_add_test(tst_image_reference backend/tst_image_reference.cpp)
kcm_docker_add_test(tst_registry_auth backend/tst_registry_auth.cpp)
kcm_docker_add_test(tst_credential_store backend/tst_credential_store.cpp)
kcm_docker_add_test(tst_docker_cli_auth_importer backend/tst_docker_cli_auth_importer.cpp)
kcm_docker_add_test(tst_dto_parsers backend/tst_dto_parsers.cpp)
kcm_docker_add_test(tst_error_mapping backend/tst_error_mapping.cpp)
kcm_docker_add_test(tst_api_version backend/tst_api_version.cpp)
kcm_docker_add_test(tst_container_model model/tst_container_model.cpp)
kcm_docker_add_test(tst_container_log_controller model/tst_container_log_controller.cpp)
kcm_docker_add_test(tst_network_model model/tst_network_model.cpp)
kcm_docker_add_test(tst_volume_model model/tst_volume_model.cpp)
kcm_docker_add_test(tst_app_config_path model/tst_app_config_path.cpp)
kcm_docker_add_test(tst_container_create model/tst_container_create.cpp)
kcm_docker_add_test(tst_filter_models model/tst_filter_models.cpp)
kcm_docker_add_test(tst_i18n_consistency model/tst_i18n_consistency.cpp)
# ki18n looks translations up at <XDG_DATA_DIRS>/locale/<lang>/LC_MESSAGES/<domain>.mo:
# adding the build dir (where ki18n generates the .mo) makes the test walk that path
target_compile_definitions(tst_i18n_consistency PRIVATE KCM_DOCKER_BUILD_DIR="${CMAKE_BINARY_DIR}")
set_tests_properties(tst_i18n_consistency PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_DATA_DIRS=${CMAKE_BINARY_DIR}:$ENV{XDG_DATA_DIRS};LANGUAGE=zh_CN")
# The runtime case reads the compiled .mo: depend on pofiles so a stale .mo cannot pass a po edit
if(TARGET pofiles)
    add_dependencies(tst_i18n_consistency pofiles)
endif()
kcm_docker_add_test(tst_source_conventions model/tst_source_conventions.cpp)
kcm_docker_add_test(tst_metrics model/tst_metrics.cpp)
kcm_docker_add_test(tst_detail_controllers model/tst_detail_controllers.cpp)
kcm_docker_add_test(tst_format model/tst_format.cpp)
kcm_docker_add_test(tst_presentation model/tst_presentation.cpp)
kcm_docker_add_test(tst_status_controller model/tst_status_controller.cpp)
kcm_docker_add_test(tst_operation_controller model/tst_operation_controller.cpp)
kcm_docker_add_test(tst_registry_auth_controller model/tst_registry_auth_controller.cpp)
kcm_docker_add_test(tst_mount_list_model model/tst_mount_list_model.cpp)
kcm_docker_add_test(tst_port_mapping_model model/tst_port_mapping_model.cpp)
kcm_docker_add_test(tst_host_port_usage model/tst_host_port_usage.cpp)
kcm_docker_add_test(tst_qml_load ui/tst_qml_load.cpp)
# The config page case asserts "user config writable → controls editable", but whether
# ~/.config is writable depends on the environment (read-only in CI): give it its own HOME.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tst-home")
# Note: this line **replaces** ENVIRONMENT wholesale, so the LANGUAGE pinned by
# kcm_docker_add_test must be repeated — or cases follow the dev machine's LANG.
set_tests_properties(tst_qml_load PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en_US;HOME=${CMAKE_BINARY_DIR}/tst-home")

# Loads the UI from the qrc, exactly like the plugin at runtime.
#
# Why this test is required: tst_qml_load reads the .qml from the source tree,
# the plugin reads the same content from the qrc. The two diverge when a new
# component is missing from the resource list or qmldir / singletons fail to
# resolve under qrc — only the installed plugin breaks, source tests stay green.
#
# Note: qt_add_resources must be called after the target exists, else Qt parses
# the args with the legacy <outfiles> signature and reports something obscure.
kcm_docker_add_test(tst_qml_resource ui/tst_qml_resource.cpp)
kcm_docker_add_test(tst_refresh_churn ui/tst_refresh_churn.cpp)

# kcmshell6 hosts the KCM's QML in a QQuickWidget (offscreen rendering + manual
# polish), a different render path than QQuickWindow, hence its own test target.
find_package(Qt6 6.5 REQUIRED COMPONENTS QuickWidgets)
kcm_docker_add_test(tst_kcm_widget_churn ui/tst_kcm_widget_churn.cpp)
target_link_libraries(tst_kcm_widget_churn PRIVATE Qt6::QuickWidgets Qt6::Widgets)
qt_add_resources(tst_qml_resource "tst_qml_resource_qml"
    PREFIX "/kcm/kcm_docker"
    BASE "${KCM_DOCKER_QML_BASE_DIR}"
    FILES
        ${KCM_DOCKER_QML_FILES}
)
kcm_docker_add_test(tst_docker_backend_against_fake_engine backend/tst_docker_backend_against_fake_engine.cpp)
kcm_docker_add_test(tst_docker_backend_integration backend/tst_docker_backend_integration.cpp)

# --- Dev tool: offscreen rendering -------------------------------------------
# Renders the UI to PNG for manual visual review (light / dark theme).
# Not registered as a ctest case: it produces images for humans, not assertions.
find_package(KF6 6.5 REQUIRED COMPONENTS IconThemes)
add_executable(kcm_docker_render_ui tools/render_ui.cpp)
target_link_libraries(kcm_docker_render_ui PRIVATE kcm_docker_core kontainer_test_support Qt6::Quick KF6::IconThemes)
target_compile_definitions(kcm_docker_render_ui PRIVATE KCM_DOCKER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
