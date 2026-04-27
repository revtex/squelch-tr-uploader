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

# ---------------------------------------------------------------------------
# Trunk-Recorder Plugin_Api headers
#
# We need a small subset of TR's public headers to compile against `Plugin_Api`.
# Rather than vendor a snapshot in our git tree, we pull the upstream source
# at configure time via FetchContent and put TR's `lib/` on the include path.
#
# Pinned to an exact tag so contributors get a reproducible build. Bump
# SQUELCH_TR_TAG to refresh; CMake will re-fetch into the build dir.
# ---------------------------------------------------------------------------
set(SQUELCH_TR_REPO  "https://github.com/robotastic/trunk-recorder.git"
    CACHE STRING "Trunk-Recorder upstream repo")
set(SQUELCH_TR_TAG   "v5.2.1"
    CACHE STRING "Trunk-Recorder tag/branch/commit to compile against")

include(FetchContent)
FetchContent_Declare(
    trunk_recorder
    GIT_REPOSITORY ${SQUELCH_TR_REPO}
    GIT_TAG        ${SQUELCH_TR_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
# We only need headers — never invoke TR's CMakeLists.
FetchContent_GetProperties(trunk_recorder)
if(NOT trunk_recorder_POPULATED)
    FetchContent_Populate(trunk_recorder)
endif()

set(SQUELCH_TR_INCLUDE_DIR "${trunk_recorder_SOURCE_DIR}"
    CACHE INTERNAL "Path to Trunk-Recorder public headers (root contains trunk-recorder/)")

find_package(CURL REQUIRED)
find_package(nlohmann_json 3 QUIET)

# Boost — TR's Plugin_Api transitively pulls in boost/log, boost/program_options,
# boost/property_tree, etc. We export the plugin factory with BOOST_DLL_ALIAS,
# which expects boost::dll headers (header-only) plus boost_log for the runtime
# logging used by parse_config / lifecycle hooks.
set(SQUELCH_BOOST_COMPONENTS log log_setup system filesystem regex thread)
find_package(Boost REQUIRED COMPONENTS ${SQUELCH_BOOST_COMPONENTS})

add_library(squelch_uploader SHARED
    src/squelch_uploader.cc
    src/config.cc
    src/http_client.cc
    src/http_client_util.cc
    src/upload_request.cc
)

target_include_directories(squelch_uploader PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# TR's headers — PRIVATE because consumers of our shared library should not
# transitively pick up TR's include tree. TR's plugin_api.h does
# `#include <json.hpp>` (no nlohmann/ prefix) so we add TR's `lib/` directory
# too — it ships nlohmann's single-header copy there.
target_include_directories(squelch_uploader PRIVATE
    ${SQUELCH_TR_INCLUDE_DIR}
    ${SQUELCH_TR_INCLUDE_DIR}/lib
)

target_link_libraries(squelch_uploader PRIVATE
    CURL::libcurl
    Boost::log
    Boost::log_setup
    Boost::system
    Boost::filesystem
    Boost::regex
    Boost::thread
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
