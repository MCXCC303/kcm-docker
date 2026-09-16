# 单元测试与集成测试（ARCH_V1 §28/§29/§30）
find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# 测试用 mock backend：让 model / controller 测试不依赖真实 Docker daemon
add_library(kontainer_test_support STATIC
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
kontainer_add_test(tst_dto_parsers backend/tst_dto_parsers.cpp)
kontainer_add_test(tst_error_mapping backend/tst_error_mapping.cpp)
kontainer_add_test(tst_api_version backend/tst_api_version.cpp)
kontainer_add_test(tst_container_model model/tst_container_model.cpp)
kontainer_add_test(tst_filter_models model/tst_filter_models.cpp)
kontainer_add_test(tst_i18n_consistency model/tst_i18n_consistency.cpp)
kontainer_add_test(tst_source_conventions model/tst_source_conventions.cpp)
kontainer_add_test(tst_metrics model/tst_metrics.cpp)
kontainer_add_test(tst_detail_controllers model/tst_detail_controllers.cpp)
kontainer_add_test(tst_format model/tst_format.cpp)
kontainer_add_test(tst_status_controller model/tst_status_controller.cpp)
kontainer_add_test(tst_qml_load ui/tst_qml_load.cpp)

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
