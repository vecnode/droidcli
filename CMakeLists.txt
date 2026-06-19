cmake_minimum_required(VERSION 3.16)
project(metaagent VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

add_executable(metaagent_transition_graph_test tests/transition_graph_test.cpp)
target_link_libraries(metaagent_transition_graph_test PRIVATE metaagent)
add_test(NAME transition_graph_test COMMAND metaagent_transition_graph_test)

add_executable(metaagent_forming_types_test tests/forming_types_test.cpp)
target_link_libraries(metaagent_forming_types_test PRIVATE metaagent)
add_test(NAME forming_types_test COMMAND metaagent_forming_types_test)

add_executable(metaagent_shape_builder_polyline_test tests/shape_builder_polyline_test.cpp)
target_link_libraries(metaagent_shape_builder_polyline_test PRIVATE metaagent)
add_test(NAME shape_builder_polyline_test COMMAND metaagent_shape_builder_polyline_test)

add_executable(metaagent_actuation_composer_test tests/actuation_composer_test.cpp)
target_link_libraries(metaagent_actuation_composer_test PRIVATE metaagent)
add_test(NAME actuation_composer_test COMMAND metaagent_actuation_composer_test)

add_executable(metaagent_media_decode_test tests/media_decode_test.cpp)
target_link_libraries(metaagent_media_decode_test PRIVATE metaagent)
metaagent_stage_ffmpeg_runtime(metaagent_media_decode_test)
add_test(NAME media_decode_test COMMAND metaagent_media_decode_test)

add_executable(metaagent_corpus_test tests/corpus_test.cpp)
target_link_libraries(metaagent_corpus_test PRIVATE metaagent)
add_test(NAME corpus_test COMMAND metaagent_corpus_test)

add_executable(metaagent_camera_rig_test tests/camera_rig_test.cpp)
target_link_libraries(metaagent_camera_rig_test PRIVATE metaagent)
add_test(NAME camera_rig_test COMMAND metaagent_camera_rig_test)

add_executable(metaagent_net_handler_test tests/net_handler_test.cpp)
target_link_libraries(metaagent_net_handler_test PRIVATE metaagent)
add_test(NAME net_handler_test COMMAND metaagent_net_handler_test)

add_executable(metaagent_app_command_test tests/app_command_test.cpp)
target_link_libraries(metaagent_app_command_test PRIVATE metaagent)
add_test(NAME app_command_test COMMAND metaagent_app_command_test)

add_executable(metaagent_gui_actions_test tests/gui_actions_test.cpp)
target_link_libraries(metaagent_gui_actions_test PRIVATE metaagent)
add_test(NAME gui_actions_test COMMAND metaagent_gui_actions_test)

add_executable(metaagent_platform_client_test tests/platform_client_test.cpp)
target_link_libraries(metaagent_platform_client_test PRIVATE metaagent)
add_test(NAME platform_client_test COMMAND metaagent_platform_client_test)

add_executable(metaagent_gui_catalog_test tests/gui_catalog_test.cpp)
target_link_libraries(metaagent_gui_catalog_test PRIVATE metaagent)
add_test(NAME gui_catalog_test COMMAND metaagent_gui_catalog_test)

add_executable(metaagent_effect_catalog_test tests/effect_catalog_test.cpp)
target_link_libraries(metaagent_effect_catalog_test PRIVATE metaagent)
add_test(NAME effect_catalog_test COMMAND metaagent_effect_catalog_test)

add_executable(metaagent_host_interfaces_test tests/host_interfaces_test.cpp)
target_link_libraries(metaagent_host_interfaces_test PRIVATE metaagent)
add_test(NAME host_interfaces_test COMMAND metaagent_host_interfaces_test)

add_executable(metaagent_state_effects_test tests/state_effects_test.cpp)
target_link_libraries(metaagent_state_effects_test PRIVATE metaagent)
add_test(NAME state_effects_test COMMAND metaagent_state_effects_test)

add_executable(metaagent_visual_continuity_test tests/visual_continuity_test.cpp)
target_link_libraries(metaagent_visual_continuity_test PRIVATE metaagent)
add_test(NAME visual_continuity_test COMMAND metaagent_visual_continuity_test)

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

add_executable(metaagent_server tools/metaagent_server.cpp tools/mini_http_server.cpp tools/sync_http_client.cpp)
target_include_directories(metaagent_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
target_link_libraries(metaagent_server PRIVATE metaagent)
if(WIN32)
    target_link_libraries(metaagent_server PRIVATE ws2_32)
    target_compile_definitions(metaagent_server PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

option(METAAGENT_BUILD_APP "Build the MetaAgent desktop webview application (app/)" OFF)
if(METAAGENT_BUILD_APP)
    add_subdirectory(app)
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
