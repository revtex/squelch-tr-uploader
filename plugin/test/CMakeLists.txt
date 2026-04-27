find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json 3 QUIET)

add_executable(squelch_uploader_tests
    test_uploader.cc
    test_config.cc
    test_upload_request.cc
    test_http_client.cc
    test_retry_policy.cc
    test_upload_worker.cc
    ../src/config.cc
    ../src/http_client_util.cc
    ../src/retry_policy.cc
    ../src/upload_request.cc
    ../src/upload_worker.cc
)

target_link_libraries(squelch_uploader_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

if(nlohmann_json_FOUND)
    target_link_libraries(squelch_uploader_tests PRIVATE nlohmann_json::nlohmann_json)
endif()

target_include_directories(squelch_uploader_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

include(GoogleTest)
gtest_discover_tests(squelch_uploader_tests)
