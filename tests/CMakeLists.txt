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
kontainer_add_test(tst_metrics model/tst_metrics.cpp)
kontainer_add_test(tst_detail_controllers model/tst_detail_controllers.cpp)
kontainer_add_test(tst_format model/tst_format.cpp)
kontainer_add_test(tst_status_controller model/tst_status_controller.cpp)
kontainer_add_test(tst_qml_load ui/tst_qml_load.cpp)
kontainer_add_test(tst_docker_backend_against_fake_engine backend/tst_docker_backend_against_fake_engine.cpp)
kontainer_add_test(tst_docker_backend_integration backend/tst_docker_backend_integration.cpp)
