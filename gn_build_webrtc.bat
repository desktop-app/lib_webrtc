@echo off
setlocal EnableDelayedExpansion
set DEPOT_TOOLS_WIN_TOOLCHAIN=0

echo Configuring "out/Debug" build...

call gn gen out/Debug --ide=vs --args="!="!^
    host_cpu=\"x86\" !="^"!^
    target_os=\"win\" !="^"!^
    is_clang=false !="^"!^
    is_component_build=false !="^"!^
    is_debug=true !="^"!^
    symbol_level=2 !="^"!^
    proprietary_codecs=true !="^"!^
    use_custom_libcxx=false !="^"!^
    use_system_libjpeg=true !="^"!^
    system_libjpeg_root=\"../../../qt_5_12_8/qtbase/src/3rdparty/libjpeg\" !="^"!^
    enable_iterator_debugging=true !="^"!^
    rtc_include_tests=false !="^"!^
    rtc_build_examples=false !="^"!^
    rtc_build_tools=false !="^"!^
    rtc_build_opus=false !="^"!^
    rtc_build_ssl=false !="^"!^
    rtc_ssl_root=\"../../../openssl_1_1_1/include\" !="^"!^
    rtc_ssl_libs=[\"../../../openssl_1_1_1/out32.dbg/libssl.lib\",\"../../../openssl_1_1_1/out32/libcrypto.lib\"] !="^"!^
    rtc_builtin_ssl_root_certificates=true !="^"!^
    rtc_build_ffmpeg=false !="^"!^
    rtc_ffmpeg_root=\"../../../ffmpeg\" !="^"!^
    rtc_ffmpeg_libs=[\"../../../ffmpeg/libavutil/libavutil.a\",\"../../../ffmpeg/libavcodec/libavcodec.a\",\"../../../ffmpeg/libswscale/libswscale.a\",\"../../../ffmpeg/libswresample/libswresample.a\"] !="^"!^
    rtc_opus_root=\"../../../opus/include\" !="^"!^
    rtc_enable_protobuf=false !="^"!^
"

rem We should use bundled:
rem rtc_build_opus=false
rem rtc_include_opus=false
rem rtc_include_pulse_audio=false

rem Can we switch this off with bundled ssl?
rem rtc_builtin_ssl_root_certificates=false

rem For release build use:
rem symbol_level=1
rem is_debug=false
rem rtc_use_gtk=false
rem rtc_disable_logging=true
rem enable_iterator_debugging=false
rem rtc_disable_metrics?
rem rtc_disable_trace_events?
rem rtc_enable_external_auth?
rem rtc_enable_sctp?
rem rtc_exclude_audio_processing_module?
rem rtc_include_builtin_audio_codecs?
rem rtc_include_builtin_video_codecs?
rem rtc_include_ilbc?
rem rtc_include_internal_audio_device? Chromium doesn't use that
rem rtc_opus_variable_complexity?
rem rtc_use_h264?

echo.
echo Configuration done, now run "ninja -C out/Debug webrtc".
