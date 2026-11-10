find_package(GTest CONFIG REQUIRED)

add_executable(kirend_tests
    test_sexpr.cpp
    test_board_reader.cpp
    test_camera.cpp
    test_paths.cpp
    test_outline.cpp
)

target_link_libraries(kirend_tests PRIVATE kirend_core GTest::gtest GTest::gtest_main)
target_compile_definitions(kirend_tests PRIVATE KIREND_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

include(GoogleTest)
gtest_discover_tests(kirend_tests)
