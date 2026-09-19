# 单元测试与集成测试（ARCH_V1 §28/§29/§30）
find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# 测试用 mock backend：让 model / controller 测试不依赖真实 Docker daemon
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
    # QML 加载测试需要知道源码目录
    target_compile_definitions(${name} PRIVATE KCM_DOCKER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    # 全部测试无需 X11/Wayland。
    #
    # 同时把语言**钉到英文**：用例断言的是源码里的英文原文，而开发机上往往
    # LANG=zh_CN 并且装了本包（/usr/share/locale 里有 kcm_docker.mo），
    # 于是 i18n() 返回中文、用例集体失败——这是环境依赖，不是真的坏了。
    # tst_i18n_consistency 需要中文，它自己的 set_tests_properties 在下面覆盖这一项。
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
# 接线测试要能读到生成物（策略与安装脚本），因此需要构建目录路径，并依赖生成目标
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
# 译文是 ki18n 按 <XDG_DATA_DIRS>/locale/<lang>/LC_MESSAGES/<domain>.mo 找的：
# 把构建目录（ki18n 在那里生成 .mo）加进去，测试才能真的走一遍加载路径
target_compile_definitions(tst_i18n_consistency PRIVATE KCM_DOCKER_BUILD_DIR="${CMAKE_BINARY_DIR}")
set_tests_properties(tst_i18n_consistency PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_DATA_DIRS=${CMAKE_BINARY_DIR}:$ENV{XDG_DATA_DIRS};LANGUAGE=zh_CN")
# 运行时用例读的是编译出来的 .mo：让它依赖 pofiles，避免改了 po 却拿旧 .mo 通过
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
# 配置页用例要断言"用户级配置可写 → 控件可编辑"，而 ~/.config 是否可写取决于运行环境
# （沙箱/CI 里可能不可写）。给它一个自己的 HOME，断言才不随环境漂移。
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tst-home")
# 注意：这一行会**整体替换** ENVIRONMENT，所以要把 kontainer_add_test 里统一钉的
# LANGUAGE 一起写上——漏了的话用例会跟着开发机的 LANG 走（中文环境下集体失败）。
set_tests_properties(tst_qml_load PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en_US;HOME=${CMAKE_BINARY_DIR}/tst-home")

# 从 qrc 加载界面（与插件运行时的加载路径完全一致）。
#
# 为什么必须有这个测试：tst_qml_load 读的是源码目录里的 .qml，
# 而插件读的是打进 qrc 的同一份内容。两者在「新增组件没加进资源清单」
# 或「qmldir / 单例在 qrc 下解析失败」时会分叉——
# 这类问题只在安装后的插件里出现，源码目录测试全绿也发现不了。
#
# 注意：qt_add_resources 必须在目标已存在之后调用，
# 否则 Qt 会按「<outfiles> 形式的旧签名」解析参数并给出难以理解的报错。
kcm_docker_add_test(tst_qml_resource ui/tst_qml_resource.cpp)
kcm_docker_add_test(tst_refresh_churn ui/tst_refresh_churn.cpp)

# kcmshell6 用 QQuickWidget 承载 KCM 的 QML（离屏渲染 + 手动 polish），
# 与 QQuickWindow 是两条不同的渲染路径，因此单独一个测试目标。
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

# --- 开发工具：离屏渲染 ---------------------------------------------------------
# 把界面渲染成 PNG，用于人工视觉复核（亮 / 暗两种主题）。
# 不注册为 ctest 用例：它输出的是给人看的图片，不是断言。
find_package(KF6 6.5 REQUIRED COMPONENTS IconThemes)
add_executable(kcm_docker_render_ui tools/render_ui.cpp)
target_link_libraries(kcm_docker_render_ui PRIVATE kcm_docker_core kontainer_test_support Qt6::Quick KF6::IconThemes)
target_compile_definitions(kcm_docker_render_ui PRIVATE KCM_DOCKER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
