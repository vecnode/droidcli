# Bundled FFmpeg (not committed - see third_party/README.md).
# LGPL 2.1+: link dynamically (DLL/.so) for commercial use without GPL contagion.

set(_DROIDCLI_DEFAULT_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/third_party/ffmpeg")
if(DEFINED ENV{DROIDCLI_FFMPEG_ROOT} AND NOT "$ENV{DROIDCLI_FFMPEG_ROOT}" STREQUAL "")
    set(_DROIDCLI_DEFAULT_FFMPEG_ROOT "$ENV{DROIDCLI_FFMPEG_ROOT}")
endif()

set(DROIDCLI_FFMPEG_ROOT "${_DROIDCLI_DEFAULT_FFMPEG_ROOT}" CACHE PATH
    "Path to FFmpeg prefix (include/, lib/, bin/)")

option(DROIDCLI_FFMPEG_AUTO_DOWNLOAD "Automatically download FFmpeg when missing" ON)
option(DROIDCLI_FFMPEG_ALLOW_INSECURE_DOWNLOAD
    "Retry FFmpeg download without TLS verification when cert validation fails" ON)

if(WIN32)
    set(_DROIDCLI_DEFAULT_FFMPEG_URL
        "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-lgpl-shared.zip")
else()
    set(_DROIDCLI_DEFAULT_FFMPEG_URL "")
endif()

set(DROIDCLI_FFMPEG_URL "${_DROIDCLI_DEFAULT_FFMPEG_URL}" CACHE STRING
    "Download URL for prebuilt FFmpeg package (used when DROIDCLI_FFMPEG_AUTO_DOWNLOAD=ON)")

set(DROIDCLI_FFMPEG_ARCHIVE "${CMAKE_SOURCE_DIR}/third_party/ffmpeg_download.zip" CACHE FILEPATH
    "Archive path used when downloading FFmpeg")

function(droidcli_resolve_ffmpeg_root base_dir out_var)
    set(_resolved "")

    if(EXISTS "${base_dir}/include/libavcodec/avcodec.h")
        set(_resolved "${base_dir}")
    else()
        file(GLOB_RECURSE _candidate_headers LIST_DIRECTORIES false
            "${base_dir}/*/include/libavcodec/avcodec.h")
        list(LENGTH _candidate_headers _candidate_count)
        if(_candidate_count GREATER 0)
            list(GET _candidate_headers 0 _first_header)
            get_filename_component(_libavcodec_dir "${_first_header}" DIRECTORY)
            get_filename_component(_include_dir "${_libavcodec_dir}" DIRECTORY)
            get_filename_component(_resolved "${_include_dir}" DIRECTORY)
        endif()
    endif()

    set(${out_var} "${_resolved}" PARENT_SCOPE)
endfunction()

function(droidcli_ensure_ffmpeg)
    droidcli_resolve_ffmpeg_root("${DROIDCLI_FFMPEG_ROOT}" _resolved_root)
    if(_resolved_root)
        if(NOT _resolved_root STREQUAL DROIDCLI_FFMPEG_ROOT)
            set(DROIDCLI_FFMPEG_ROOT "${_resolved_root}" CACHE PATH
                "Path to FFmpeg prefix (include/, lib/, bin/)" FORCE)
        endif()
        return()
    endif()

    if(NOT DROIDCLI_FFMPEG_AUTO_DOWNLOAD)
        message(FATAL_ERROR
            "FFmpeg not found at ${DROIDCLI_FFMPEG_ROOT}\n"
            "Enable DROIDCLI_FFMPEG_AUTO_DOWNLOAD or provide DROIDCLI_FFMPEG_ROOT manually.\n"
            "See third_party/README.md")
    endif()

    if(NOT WIN32)
        message(FATAL_ERROR
            "FFmpeg auto-download is currently configured for Windows prebuilt archives.\n"
            "Set DROIDCLI_FFMPEG_ROOT to a local FFmpeg prefix (include/, lib/, bin/).")
    endif()

    if(DROIDCLI_FFMPEG_URL STREQUAL "")
        message(FATAL_ERROR
            "DROIDCLI_FFMPEG_URL is empty. Set it to a downloadable FFmpeg archive URL.")
    endif()

    get_filename_component(_archive_dir "${DROIDCLI_FFMPEG_ARCHIVE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_archive_dir}")

    message(STATUS "FFmpeg not found locally. Downloading from: ${DROIDCLI_FFMPEG_URL}")
    file(DOWNLOAD
        "${DROIDCLI_FFMPEG_URL}"
        "${DROIDCLI_FFMPEG_ARCHIVE}"
        SHOW_PROGRESS
        STATUS _dl_status
        TLS_VERIFY ON)

    list(GET _dl_status 0 _dl_code)
    list(GET _dl_status 1 _dl_message)

    if(NOT _dl_code EQUAL 0 AND _dl_code EQUAL 60 AND DROIDCLI_FFMPEG_ALLOW_INSECURE_DOWNLOAD)
        message(WARNING
            "FFmpeg download failed TLS verification (${_dl_message}). Retrying with TLS verification OFF.\n"
            "Set DROIDCLI_FFMPEG_ALLOW_INSECURE_DOWNLOAD=OFF to disable this fallback.")
        file(DOWNLOAD
            "${DROIDCLI_FFMPEG_URL}"
            "${DROIDCLI_FFMPEG_ARCHIVE}"
            SHOW_PROGRESS
            STATUS _dl_status
            TLS_VERIFY OFF)
        list(GET _dl_status 0 _dl_code)
        list(GET _dl_status 1 _dl_message)
    endif()

    if(NOT _dl_code EQUAL 0)
        message(FATAL_ERROR
            "Failed to download FFmpeg (${_dl_code}): ${_dl_message}\n"
            "URL: ${DROIDCLI_FFMPEG_URL}\n"
            "Try setting DROIDCLI_FFMPEG_URL to an internal mirror or pre-populate DROIDCLI_FFMPEG_ROOT.")
    endif()

    file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/third_party/ffmpeg")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/ffmpeg")
    file(ARCHIVE_EXTRACT
        INPUT "${DROIDCLI_FFMPEG_ARCHIVE}"
        DESTINATION "${CMAKE_SOURCE_DIR}/third_party/ffmpeg")

    droidcli_resolve_ffmpeg_root("${CMAKE_SOURCE_DIR}/third_party/ffmpeg" _downloaded_root)
    if(NOT _downloaded_root)
        message(FATAL_ERROR
            "FFmpeg archive extracted but include/libavcodec/avcodec.h was not found.\n"
            "Check archive layout or set DROIDCLI_FFMPEG_ROOT manually.")
    endif()

    set(DROIDCLI_FFMPEG_ROOT "${_downloaded_root}" CACHE PATH
        "Path to FFmpeg prefix (include/, lib/, bin/)" FORCE)
    message(STATUS "FFmpeg ready at: ${DROIDCLI_FFMPEG_ROOT}")
endfunction()

droidcli_ensure_ffmpeg()

function(droidcli_link_ffmpeg target)
    if(NOT EXISTS "${DROIDCLI_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
        message(FATAL_ERROR
            "FFmpeg not found at ${DROIDCLI_FFMPEG_ROOT}\n"
            "Extract FFmpeg dev libraries there. See third_party/README.md")
    endif()

    target_include_directories(${target} PUBLIC
        "$<BUILD_INTERFACE:${DROIDCLI_FFMPEG_ROOT}/include>")
    target_link_directories(${target} PUBLIC
        "$<BUILD_INTERFACE:${DROIDCLI_FFMPEG_ROOT}/lib>")
    target_link_libraries(${target} PUBLIC avformat avcodec swscale avutil)
    target_compile_definitions(${target} PUBLIC DROIDCLI_HAS_FFMPEG=1)
endfunction()

function(droidcli_stage_ffmpeg_runtime target)
    if(NOT WIN32 OR NOT EXISTS "${DROIDCLI_FFMPEG_ROOT}/bin")
        return()
    endif()

    get_target_property(_target_type ${target} TYPE)
    if(NOT _target_type STREQUAL "EXECUTABLE")
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DROIDCLI_FFMPEG_ROOT}/bin"
            $<TARGET_FILE_DIR:${target}>
        COMMENT "Copying FFmpeg runtime DLLs next to ${target}")
endfunction()
