cmake_minimum_required(VERSION 3.16)
project(droidcli VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# One MSVC runtime for the whole tree (library, app, tests, FetchContent deps):
# dynamic in Debug, static in Release so the distributed exe needs no VC redist.
# Mixing /MT and /MD across targets breaks the Release link.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<NOT:$<CONFIG:Debug>>:MultiThreaded>")
endif()

include(cmake/FFmpeg.cmake)

# Vendored SQLite amalgamation (third_party/README.md) - backs
# cli/memory_store.cpp's persistent agent-turn history. Built as its own
# tiny static library, warnings suppressed (/w) since this is unmodified
# third-party C, not code this repo's /W4 policy should apply to. Linked
# only into the droidcli executable target, never droidcli_core - real
# file I/O is a host concern per AGENTS.md's Golden rule.
add_library(droidcli_sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(droidcli_sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite)
if(MSVC)
    target_compile_options(droidcli_sqlite3 PRIVATE /w)
else()
    target_compile_options(droidcli_sqlite3 PRIVATE -w)
endif()

add_library(droidcli_core STATIC
    droidcli_core.cpp
)

target_include_directories(droidcli_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(droidcli_core
    PUBLIC
        DROIDCLI_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        DROIDCLI_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        DROIDCLI_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(MSVC)
    target_compile_options(droidcli_core PRIVATE /W4)
else()
    target_compile_options(droidcli_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

droidcli_link_ffmpeg(droidcli_core)

enable_testing()

add_executable(droidcli_media_decode_test tests/media_decode_test.cpp)
target_link_libraries(droidcli_media_decode_test PRIVATE droidcli_core)
droidcli_stage_ffmpeg_runtime(droidcli_media_decode_test)
add_test(NAME media_decode_test COMMAND droidcli_media_decode_test)

add_executable(droidcli_net_handler_test tests/net_handler_test.cpp)
target_link_libraries(droidcli_net_handler_test PRIVATE droidcli_core)
add_test(NAME net_handler_test COMMAND droidcli_net_handler_test)

add_executable(droidcli_ollama_client_test tests/ollama_client_test.cpp)
target_link_libraries(droidcli_ollama_client_test PRIVATE droidcli_core)
add_test(NAME ollama_client_test COMMAND droidcli_ollama_client_test)

add_executable(droidcli_language_runtime_test tests/language_runtime_test.cpp)
target_link_libraries(droidcli_language_runtime_test PRIVATE droidcli_core)
add_test(NAME language_runtime_test COMMAND droidcli_language_runtime_test)

add_executable(droidcli_connector_test tests/connector_test.cpp)
target_link_libraries(droidcli_connector_test PRIVATE droidcli_core)
add_test(NAME connector_test COMMAND droidcli_connector_test)

add_executable(droidcli_task_queue_test tests/task_queue_test.cpp)
target_link_libraries(droidcli_task_queue_test PRIVATE droidcli_core)
add_test(NAME task_queue_test COMMAND droidcli_task_queue_test)

add_executable(droidcli_intent_test tests/intent_test.cpp)
target_link_libraries(droidcli_intent_test PRIVATE droidcli_core)
add_test(NAME intent_test COMMAND droidcli_intent_test)

add_executable(droidcli_pending_command_test tests/pending_command_test.cpp)
target_link_libraries(droidcli_pending_command_test PRIVATE droidcli_core)
add_test(NAME pending_command_test COMMAND droidcli_pending_command_test)

add_executable(droidcli_model_provider_test tests/model_provider_test.cpp)
target_link_libraries(droidcli_model_provider_test PRIVATE droidcli_core)
add_test(NAME model_provider_test COMMAND droidcli_model_provider_test)

add_executable(droidcli_spawn_test tests/spawn_test.cpp)
target_link_libraries(droidcli_spawn_test PRIVATE droidcli_core)
add_test(NAME spawn_test COMMAND droidcli_spawn_test)

# FTXUI powers droidcli's default interactive terminal dashboard (cli/tui.cpp).
# Scoped to the droidcli target only via FetchContent + target_link_libraries -
# it must never leak into the core `droidcli_core` library or the test targets.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_MODULES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v7.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ftxui)

add_executable(droidcli
    cli/app_index.cpp
    cli/command_runner.cpp
    cli/droidcli.cpp
    cli/ffmpeg_tool.cpp
    cli/filesystem_tools.cpp
    cli/hardware_info.cpp
    cli/host.cpp
    cli/http_mount.cpp
    cli/memory_store.cpp
    cli/process_manager.cpp
    cli/system_info.cpp
    cli/tui.cpp
    cli/window_list.cpp
    tools/mini_http_server.cpp
    tools/sync_http_client.cpp
)
target_include_directories(droidcli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools
        ${CMAKE_CURRENT_SOURCE_DIR}/cli
)
target_link_libraries(droidcli PRIVATE droidcli_core droidcli_sqlite3 ftxui::ftxui)
if(WIN32)
    target_link_libraries(droidcli PRIVATE ws2_32 winhttp advapi32 ole32 shell32)
    target_compile_definitions(droidcli PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

install(TARGETS droidcli_core
    EXPORT droidcliTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES droidcli_core.h DESTINATION include)
install(DIRECTORY src/ DESTINATION include)

install(EXPORT droidcliTargets
    FILE droidcliTargets.cmake
    NAMESPACE droidcli::
    DESTINATION lib/cmake/droidcli
)
