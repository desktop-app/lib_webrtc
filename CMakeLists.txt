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
    webrtc/webrtc_media_devices.cpp
    webrtc/webrtc_media_devices.h
    webrtc/webrtc_video_track.cpp
    webrtc/webrtc_video_track.h
    webrtc/webrtc_video_track_dummy.cpp

    webrtc/mac/webrtc_media_devices_mac.h
    webrtc/mac/webrtc_media_devices_mac.mm
)

if (APPLE)
    target_compile_definitions(lib_webrtc
    PRIVATE
        WEBRTC_MAC
    )
endif()

target_include_directories(lib_webrtc
PUBLIC
    ${src_loc}
)

if (DESKTOP_APP_DISABLE_WEBRTC_INTEGRATION)
    remove_target_sources(lib_webrtc ${src_loc}
        webrtc/webrtc_video_track.cpp
    )
else()
    remove_target_sources(lib_webrtc ${src_loc}
        webrtc/webrtc_video_track_dummy.cpp
    )
    target_link_libraries(lib_webrtc
    PRIVATE
        desktop-app::external_webrtc
    )
endif()

target_link_libraries(lib_webrtc
PUBLIC
    desktop-app::lib_base
    desktop-app::lib_ffmpeg
    desktop-app::external_qt
)
