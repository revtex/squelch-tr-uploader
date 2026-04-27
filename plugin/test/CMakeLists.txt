find_package(GTest REQUIRED)

add_executable(squelch_uploader_tests
    test_uploader.cc
)

target_link_libraries(squelch_uploader_tests PRIVATE
    squelch_uploader
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(squelch_uploader_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

include(GoogleTest)
gtest_discover_tests(squelch_uploader_tests)
