cmake_minimum_required(VERSION 3.16)
project(metaagent VERSION 0.2.0 LANGUAGES CXX)

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

add_library(metaagent STATIC
    metaagent.cpp
)

target_include_directories(metaagent
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(metaagent
    PUBLIC
        METAAGENT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        METAAGENT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        METAAGENT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(MSVC)
    target_compile_options(metaagent PRIVATE /W4)
else()
    target_compile_options(metaagent PRIVATE -Wall -Wextra -Wpedantic)
endif()

metaagent_link_ffmpeg(metaagent)

enable_testing()

add_executable(metaagent_media_decode_test tests/media_decode_test.cpp)
target_link_libraries(metaagent_media_decode_test PRIVATE metaagent)
metaagent_stage_ffmpeg_runtime(metaagent_media_decode_test)
add_test(NAME media_decode_test COMMAND metaagent_media_decode_test)

add_executable(metaagent_corpus_test tests/corpus_test.cpp)
target_link_libraries(metaagent_corpus_test PRIVATE metaagent)
add_test(NAME corpus_test COMMAND metaagent_corpus_test)

add_executable(metaagent_net_handler_test tests/net_handler_test.cpp)
target_link_libraries(metaagent_net_handler_test PRIVATE metaagent)
add_test(NAME net_handler_test COMMAND metaagent_net_handler_test)

add_executable(metaagent_app_command_test tests/app_command_test.cpp)
target_link_libraries(metaagent_app_command_test PRIVATE metaagent)
add_test(NAME app_command_test COMMAND metaagent_app_command_test)

add_executable(metaagent_host_interfaces_test tests/host_interfaces_test.cpp)
target_link_libraries(metaagent_host_interfaces_test PRIVATE metaagent)
add_test(NAME host_interfaces_test COMMAND metaagent_host_interfaces_test)

add_executable(metaagent_ollama_client_test tests/ollama_client_test.cpp)
target_link_libraries(metaagent_ollama_client_test PRIVATE metaagent)
add_test(NAME ollama_client_test COMMAND metaagent_ollama_client_test)

add_executable(metaagent_language_runtime_test tests/language_runtime_test.cpp)
target_link_libraries(metaagent_language_runtime_test PRIVATE metaagent)
add_test(NAME language_runtime_test COMMAND metaagent_language_runtime_test)

add_executable(metaagent_signal_router_test tests/signal_router_test.cpp)
target_link_libraries(metaagent_signal_router_test PRIVATE metaagent)
add_test(NAME signal_router_test COMMAND metaagent_signal_router_test)

add_executable(metaagent_runtime_catalog_test tests/runtime_catalog_test.cpp)
target_link_libraries(metaagent_runtime_catalog_test PRIVATE metaagent)
add_test(NAME runtime_catalog_test COMMAND metaagent_runtime_catalog_test)

add_executable(metaagent_google_search_test tests/google_search_test.cpp)
target_link_libraries(metaagent_google_search_test PRIVATE metaagent)
add_test(NAME google_search_test COMMAND metaagent_google_search_test)

add_executable(metaagent_connector_test tests/connector_test.cpp)
target_link_libraries(metaagent_connector_test PRIVATE metaagent)
add_test(NAME connector_test COMMAND metaagent_connector_test)

add_executable(metaagent_task_queue_test tests/task_queue_test.cpp)
target_link_libraries(metaagent_task_queue_test PRIVATE metaagent)
add_test(NAME task_queue_test COMMAND metaagent_task_queue_test)

# FTXUI powers droidcli's default interactive terminal dashboard (cli/tui.cpp).
# Scoped to the droidcli target only via FetchContent + target_link_libraries -
# it must never leak into the core `metaagent` library or the test targets.
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
target_link_libraries(droidcli PRIVATE metaagent ftxui::ftxui)
if(WIN32)
    target_link_libraries(droidcli PRIVATE ws2_32 winhttp)
    target_compile_definitions(droidcli PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

install(TARGETS metaagent
    EXPORT metaagentTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES metaagent.h DESTINATION include)
install(DIRECTORY src/ DESTINATION include)

install(EXPORT metaagentTargets
    FILE metaagentTargets.cmake
    NAMESPACE metaagent::
    DESTINATION lib/cmake/metaagent
)
