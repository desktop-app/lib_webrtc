# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

add_library(lib_webrtc OBJECT)
add_library(desktop-app::lib_webrtc ALIAS lib_webrtc)
init_target(lib_webrtc)

get_filename_component(src_loc . REALPATH)

target_precompile_headers(lib_webrtc PRIVATE ${src_loc}/webrtc/webrtc_pch.h)
nice_target_sources(lib_webrtc ${src_loc}
PRIVATE
    webrtc/webrtc_audio_input_tester.cpp
    webrtc/webrtc_audio_input_tester.h
    webrtc/webrtc_create_adm.cpp
    webrtc/webrtc_create_adm.h
    webrtc/webrtc_device_common.h
    webrtc/webrtc_device_id.cpp
    webrtc/webrtc_device_id.h
    webrtc/webrtc_environment.cpp
    webrtc/webrtc_environment.h
    webrtc/webrtc_video_track.cpp
    webrtc/webrtc_video_track.h

    webrtc/details/webrtc_environment_openal.cpp
    webrtc/details/webrtc_environment_openal.h
    webrtc/details/webrtc_environment_video_capture.cpp
    webrtc/details/webrtc_environment_video_capture.h
    webrtc/details/webrtc_openal_adm.cpp
    webrtc/details/webrtc_openal_adm.h

    webrtc/platform/linux/webrtc_environment_linux.cpp
    webrtc/platform/linux/webrtc_environment_linux.h
    webrtc/platform/mac/webrtc_environment_mac.h
    webrtc/platform/mac/webrtc_environment_mac.mm
    webrtc/platform/win/webrtc_environment_win.cpp
    webrtc/platform/win/webrtc_environment_win.h
    webrtc/platform/win/webrtc_loopback_adm_win.cpp
    webrtc/platform/win/webrtc_loopback_adm_win.h
    webrtc/platform/webrtc_platform_environment.h

    webrtc/webrtc_pch.h
)

if (WIN32)
    target_compile_definitions(lib_webrtc
    PRIVATE
        WEBRTC_WIN
    )
    nuget_add_winrt(lib_webrtc)
elseif (APPLE)
    target_compile_definitions(lib_webrtc
    PRIVATE
        WEBRTC_MAC
    )
endif()

target_include_directories(lib_webrtc
PUBLIC
    ${src_loc}
)

target_link_libraries(lib_webrtc
PUBLIC
    desktop-app::lib_base
    desktop-app::lib_ffmpeg
    desktop-app::external_qt
    desktop-app::external_openal
PRIVATE
    desktop-app::external_webrtc
)
