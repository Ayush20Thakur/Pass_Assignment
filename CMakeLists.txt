cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(Forge
    VERSION 1.0.0
    DESCRIPTION "Unified Engineering Workbench"
    LANGUAGES CXX C
)

# Enforce C++17 across all targets
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Target MSVC /MT static runtime to eliminate dynamic VC runtime dependencies
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /permissive- /utf-8)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Dependency Management via FetchContent with strictly pinned tags and release archives
include(FetchContent)

# 1. doctest (v2.4.11)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.zip
)

# 2. nlohmann_json (v3.11.3)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip
)

# 3. cpp-httplib (v0.18.3)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.zip
)

FetchContent_MakeAvailable(doctest nlohmann_json httplib)

# ---------------------------------------------------------------------------
# Target 1: forge_core (Static Library)
# Note: HARD RULE 5: core/ never includes HTTP, JSON, or UI headers.
# ---------------------------------------------------------------------------
add_library(forge_core STATIC
    src/core/errors.hpp
    src/core/version.hpp
    src/core/version.cpp
    src/core/geometry_types.hpp
    src/core/fem1d.hpp
    src/core/fem1d.cpp
    src/core/stl.hpp
    src/core/stl.cpp
    src/core/csv.hpp
    src/core/csv.cpp
    src/core/threadpool.hpp
    src/core/batch.hpp
    src/core/batch.cpp
    src/core/devices.hpp
    src/core/devices.cpp
)

target_include_directories(forge_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

find_package(Threads REQUIRED)
target_link_libraries(forge_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# Target 2: forge_server (HTTP / WebSocket / SSE Backend Server)
# ---------------------------------------------------------------------------
add_executable(forge_server
    src/server/job_manager.hpp
    src/server/job_manager.cpp
    src/server/server.hpp
    src/server/server.cpp
    src/server/main.cpp
)

target_link_libraries(forge_server PRIVATE
    forge_core
    httplib::httplib
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(forge_server PRIVATE ws2_32 crypt32)
endif()

# ---------------------------------------------------------------------------
# Target 3: forge_tests (Unit and Integration Test Runner)
# ---------------------------------------------------------------------------
enable_testing()
include(CTest)

add_executable(forge_tests
    tests/smoke_test.cpp
    tests/core/fem1d_test.cpp
    tests/core/stl_test.cpp
    tests/core/csv_test.cpp
    tests/core/threadpool_test.cpp
    tests/core/batch_test.cpp
    tests/core/devices_test.cpp
    tests/server/server_test.cpp
    src/server/job_manager.cpp
    src/server/server.cpp
)

target_link_libraries(forge_tests PRIVATE
    forge_core
    httplib::httplib
    nlohmann_json::nlohmann_json
    doctest::doctest
)

if(WIN32)
    target_link_libraries(forge_tests PRIVATE ws2_32 crypt32)
endif()

add_test(NAME core_tests COMMAND forge_tests)
set_tests_properties(core_tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# ---------------------------------------------------------------------------
# Target 4: forge_launcher (Win32 + WebView2 Desktop Launcher -> Launch.exe)
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(forge_launcher WIN32
        src/launcher/main.cpp
    )
else()
    add_executable(forge_launcher
        src/launcher/main.cpp
    )
endif()

set_target_properties(forge_launcher PROPERTIES
    OUTPUT_NAME "Launch"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/Debug"
)

target_link_libraries(forge_launcher PRIVATE
    forge_core
)
