cmake_minimum_required(VERSION 3.20)
project(squelch_uploader
        VERSION 0.1.0
        DESCRIPTION "Squelch uploader plugin for Trunk-Recorder"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SQUELCH_BUILD_TESTS "Build unit tests" OFF)

# Trunk-Recorder Plugin_Api headers must be on the include path. For now we
# accept them being injected via TR_PLUGIN_API_INCLUDE; future work will vendor
# a pinned copy under third_party/.
if(DEFINED ENV{TR_PLUGIN_API_INCLUDE})
    include_directories($ENV{TR_PLUGIN_API_INCLUDE})
endif()

find_package(CURL REQUIRED)
find_package(nlohmann_json 3 QUIET)

add_library(squelch_uploader SHARED
    src/squelch_uploader.cc
)

target_include_directories(squelch_uploader PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(squelch_uploader PRIVATE
    CURL::libcurl
)

if(nlohmann_json_FOUND)
    target_link_libraries(squelch_uploader PRIVATE nlohmann_json::nlohmann_json)
endif()

set_target_properties(squelch_uploader PROPERTIES
    PREFIX ""
    OUTPUT_NAME "squelch_uploader"
)

if(SQUELCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

install(TARGETS squelch_uploader
        LIBRARY DESTINATION lib/trunk-recorder/plugins)
