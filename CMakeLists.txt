cmake_minimum_required(VERSION 3.16)
project(droidcli VERSION 0.2.0 LANGUAGES CXX)

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

add_library(droidcli_core STATIC
    droidcli.cpp
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

add_executable(droidcli_corpus_test tests/corpus_test.cpp)
target_link_libraries(droidcli_corpus_test PRIVATE droidcli_core)
add_test(NAME corpus_test COMMAND droidcli_corpus_test)

add_executable(droidcli_net_handler_test tests/net_handler_test.cpp)
target_link_libraries(droidcli_net_handler_test PRIVATE droidcli_core)
add_test(NAME net_handler_test COMMAND droidcli_net_handler_test)

add_executable(droidcli_app_command_test tests/app_command_test.cpp)
target_link_libraries(droidcli_app_command_test PRIVATE droidcli_core)
add_test(NAME app_command_test COMMAND droidcli_app_command_test)

add_executable(droidcli_host_interfaces_test tests/host_interfaces_test.cpp)
target_link_libraries(droidcli_host_interfaces_test PRIVATE droidcli_core)
add_test(NAME host_interfaces_test COMMAND droidcli_host_interfaces_test)

add_executable(droidcli_ollama_client_test tests/ollama_client_test.cpp)
target_link_libraries(droidcli_ollama_client_test PRIVATE droidcli_core)
add_test(NAME ollama_client_test COMMAND droidcli_ollama_client_test)

add_executable(droidcli_language_runtime_test tests/language_runtime_test.cpp)
target_link_libraries(droidcli_language_runtime_test PRIVATE droidcli_core)
add_test(NAME language_runtime_test COMMAND droidcli_language_runtime_test)

add_executable(droidcli_signal_router_test tests/signal_router_test.cpp)
target_link_libraries(droidcli_signal_router_test PRIVATE droidcli_core)
add_test(NAME signal_router_test COMMAND droidcli_signal_router_test)

add_executable(droidcli_runtime_catalog_test tests/runtime_catalog_test.cpp)
target_link_libraries(droidcli_runtime_catalog_test PRIVATE droidcli_core)
add_test(NAME runtime_catalog_test COMMAND droidcli_runtime_catalog_test)

add_executable(droidcli_google_search_test tests/google_search_test.cpp)
target_link_libraries(droidcli_google_search_test PRIVATE droidcli_core)
add_test(NAME google_search_test COMMAND droidcli_google_search_test)

add_executable(droidcli_connector_test tests/connector_test.cpp)
target_link_libraries(droidcli_connector_test PRIVATE droidcli_core)
add_test(NAME connector_test COMMAND droidcli_connector_test)

add_executable(droidcli_task_queue_test tests/task_queue_test.cpp)
target_link_libraries(droidcli_task_queue_test PRIVATE droidcli_core)
add_test(NAME task_queue_test COMMAND droidcli_task_queue_test)

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
    cli/droidcli.cpp
    cli/host.cpp
    cli/http_mount.cpp
    cli/process_manager.cpp
    cli/tui.cpp
    tools/mini_http_server.cpp
    tools/sync_http_client.cpp
)
target_include_directories(droidcli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools
        ${CMAKE_CURRENT_SOURCE_DIR}/cli
)
target_link_libraries(droidcli PRIVATE droidcli_core ftxui::ftxui)
if(WIN32)
    target_link_libraries(droidcli PRIVATE ws2_32 winhttp)
    target_compile_definitions(droidcli PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

install(TARGETS droidcli_core
    EXPORT droidcliTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES droidcli.h DESTINATION include)
install(DIRECTORY src/ DESTINATION include)

install(EXPORT droidcliTargets
    FILE droidcliTargets.cmake
    NAMESPACE droidcli::
    DESTINATION lib/cmake/droidcli
)
