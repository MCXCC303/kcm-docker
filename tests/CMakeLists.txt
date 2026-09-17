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
target_link_libraries(kontainer_test_support PUBLIC kontainer_core Qt6::Test Qt6::Qml)

function(kontainer_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE kontainer_core kontainer_test_support Qt6::Test Qt6::Quick)
    # QML 加载测试需要知道源码目录
    target_compile_definitions(${name} PRIVATE KONTAINER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    # 全部测试无需 X11/Wayland
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

kontainer_add_test(tst_http_response_parser backend/tst_http_response_parser.cpp)
kontainer_add_test(tst_json_line_reader backend/tst_json_line_reader.cpp)
kontainer_add_test(tst_log_frame_reader backend/tst_log_frame_reader.cpp)
kontainer_add_test(tst_daemon_config backend/tst_daemon_config.cpp)
kontainer_add_test(tst_daemon_deployment backend/tst_daemon_deployment.cpp)
kontainer_add_test(tst_kontainer_helper backend/tst_kontainer_helper.cpp)
kontainer_add_test(tst_kauth_wiring backend/tst_kauth_wiring.cpp)
# 接线测试要能读到生成物（策略与安装脚本），因此需要构建目录路径，并依赖生成目标
target_compile_definitions(tst_kauth_wiring PRIVATE
    KONTAINER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    KONTAINER_BUILD_DIR="${CMAKE_BINARY_DIR}")
if(TARGET kontainer_policy)
    add_dependencies(tst_kauth_wiring kontainer_policy)
endif()
kontainer_add_test(tst_docker_capabilities backend/tst_docker_capabilities.cpp)
kontainer_add_test(tst_image_reference backend/tst_image_reference.cpp)
kontainer_add_test(tst_registry_auth backend/tst_registry_auth.cpp)
kontainer_add_test(tst_credential_store backend/tst_credential_store.cpp)
kontainer_add_test(tst_docker_cli_auth_importer backend/tst_docker_cli_auth_importer.cpp)
kontainer_add_test(tst_dto_parsers backend/tst_dto_parsers.cpp)
kontainer_add_test(tst_error_mapping backend/tst_error_mapping.cpp)
kontainer_add_test(tst_api_version backend/tst_api_version.cpp)
kontainer_add_test(tst_container_model model/tst_container_model.cpp)
kontainer_add_test(tst_filter_models model/tst_filter_models.cpp)
kontainer_add_test(tst_i18n_consistency model/tst_i18n_consistency.cpp)
# 译文是 ki18n 按 <XDG_DATA_DIRS>/locale/<lang>/LC_MESSAGES/<domain>.mo 找的：
# 把构建目录（ki18n 在那里生成 .mo）加进去，测试才能真的走一遍加载路径
target_compile_definitions(tst_i18n_consistency PRIVATE KONTAINER_BUILD_DIR="${CMAKE_BINARY_DIR}")
set_tests_properties(tst_i18n_consistency PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_DATA_DIRS=${CMAKE_BINARY_DIR}:$ENV{XDG_DATA_DIRS};LANGUAGE=zh_CN")
# 运行时用例读的是编译出来的 .mo：让它依赖 pofiles，避免改了 po 却拿旧 .mo 通过
if(TARGET pofiles)
    add_dependencies(tst_i18n_consistency pofiles)
endif()
kontainer_add_test(tst_source_conventions model/tst_source_conventions.cpp)
kontainer_add_test(tst_metrics model/tst_metrics.cpp)
kontainer_add_test(tst_detail_controllers model/tst_detail_controllers.cpp)
kontainer_add_test(tst_format model/tst_format.cpp)
kontainer_add_test(tst_presentation model/tst_presentation.cpp)
kontainer_add_test(tst_status_controller model/tst_status_controller.cpp)
kontainer_add_test(tst_operation_controller model/tst_operation_controller.cpp)
kontainer_add_test(tst_registry_auth_controller model/tst_registry_auth_controller.cpp)
kontainer_add_test(tst_mount_list_model model/tst_mount_list_model.cpp)
kontainer_add_test(tst_port_mapping_model model/tst_port_mapping_model.cpp)
kontainer_add_test(tst_qml_load ui/tst_qml_load.cpp)
# 配置页用例要断言"用户级配置可写 → 控件可编辑"，而 ~/.config 是否可写取决于运行环境
# （沙箱/CI 里可能不可写）。给它一个自己的 HOME，断言才不随环境漂移。
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tst-home")
set_tests_properties(tst_qml_load PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${CMAKE_BINARY_DIR}/tst-home")

# 从 qrc 加载界面（与插件运行时的加载路径完全一致）。
#
# 为什么必须有这个测试：tst_qml_load 读的是源码目录里的 .qml，
# 而插件读的是打进 qrc 的同一份内容。两者在「新增组件没加进资源清单」
# 或「qmldir / 单例在 qrc 下解析失败」时会分叉——
# 这类问题只在安装后的插件里出现，源码目录测试全绿也发现不了。
#
# 注意：qt_add_resources 必须在目标已存在之后调用，
# 否则 Qt 会按「<outfiles> 形式的旧签名」解析参数并给出难以理解的报错。
kontainer_add_test(tst_qml_resource ui/tst_qml_resource.cpp)
kontainer_add_test(tst_refresh_churn ui/tst_refresh_churn.cpp)

# kcmshell6 用 QQuickWidget 承载 KCM 的 QML（离屏渲染 + 手动 polish），
# 与 QQuickWindow 是两条不同的渲染路径，因此单独一个测试目标。
find_package(Qt6 6.5 REQUIRED COMPONENTS QuickWidgets)
kontainer_add_test(tst_kcm_widget_churn ui/tst_kcm_widget_churn.cpp)
target_link_libraries(tst_kcm_widget_churn PRIVATE Qt6::QuickWidgets Qt6::Widgets)
qt_add_resources(tst_qml_resource "tst_qml_resource_qml"
    PREFIX "/kcm/kcm_docker"
    BASE "${KONTAINER_QML_BASE_DIR}"
    FILES
        ${KONTAINER_QML_FILES}
)
kontainer_add_test(tst_docker_backend_against_fake_engine backend/tst_docker_backend_against_fake_engine.cpp)
kontainer_add_test(tst_docker_backend_integration backend/tst_docker_backend_integration.cpp)

# --- 开发工具：离屏渲染 ---------------------------------------------------------
# 把界面渲染成 PNG，用于人工视觉复核（亮 / 暗两种主题）。
# 不注册为 ctest 用例：它输出的是给人看的图片，不是断言。
find_package(KF6 6.5 REQUIRED COMPONENTS IconThemes)
add_executable(kontainer_render_ui tools/render_ui.cpp)
target_link_libraries(kontainer_render_ui PRIVATE kontainer_core kontainer_test_support Qt6::Quick KF6::IconThemes)
target_compile_definitions(kontainer_render_ui PRIVATE KONTAINER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
