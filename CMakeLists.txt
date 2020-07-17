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
    webrtc/webrtc_video_track.cpp
    webrtc/webrtc_video_track.h
)

target_include_directories(lib_webrtc
PUBLIC
    ${src_loc}
)

target_link_libraries(lib_webrtc
PRIVATE
    desktop-app::external_webrtc
PUBLIC
    desktop-app::lib_base
    desktop-app::lib_ffmpeg
    desktop-app::external_qt
)
