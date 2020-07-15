echo Configuring "out/Debug" build...

if [[ "$OSTYPE" == "darwin"* ]]; then
     ArgumentsList=`echo \
        host_cpu=\"x64\" \
        target_os=\"mac\" \
        is_component_build=false \
        is_debug=true \
        symbol_level=2 \
        proprietary_codecs=true \
        use_custom_libcxx=false \
        use_system_libjpeg=true \
        system_libjpeg_root=\"../../../qt_5_12_8/qtbase/src/3rdparty/libjpeg\" \
        use_xcode_clang=true \
        use_rtti=true \
        enable_iterator_debugging=true \
        enable_dsyms=true \
        mac_deployment_target=\"10.12.0\" \
        rtc_include_tests=true \
        rtc_build_examples=true \
        rtc_build_tools=true \
        rtc_build_opus=true \
        rtc_build_ssl=false \
        rtc_ssl_root=\"../../../openssl_1_1_1/include\" \
        rtc_ssl_libs=[\"../../../openssl_1_1_1/libssl.a\",\"../../../openssl_1_1_1/libcrypto.a\"] \
        rtc_builtin_ssl_root_certificates=true \
        rtc_build_ffmpeg=false \
        rtc_ffmpeg_root=\"../../../ffmpeg\" \
        rtc_ffmpeg_libs=[\"../../../ffmpeg/libavcodec/libavcodec.a\",\"../../../ffmpeg/libswscale/libswscale.a\",\"../../../ffmpeg/libswresample/libswresample.a\",\"../../../ffmpeg/libavutil/libavutil.a\",\"/usr/local/macos/lib/libiconv.a\",\"CoreVideo.framework\"] \
        rtc_enable_protobuf=false`

   gn gen out/Debug --ide=xcode --args="$ArgumentsList"
else
    ArgumentsList=`echo \
        host_cpu=\"x64\" \
        target_os=\"linux\" \
        is_component_build=false \
        is_debug=true \
        is_clang=false \
        symbol_level=2 \
        gtk_version=2 \
        proprietary_codecs=true \
        use_custom_libcxx=false \
        use_libjpeg_turbo=false \
        use_rtti=true \
        use_gold=false \
        use_sysroot=false \
        linux_use_bundled_binutils=false \
        enable_dsyms=true \
        rtc_include_tests=true \
        rtc_build_examples=true \
        rtc_build_tools=true \
        rtc_build_opus=true \
        rtc_build_ssl=false \
        rtc_ssl_root=\"../../../openssl_1_1_1/include\" \
        rtc_ssl_libs=[\"../../../openssl_1_1_1/libssl.a\",\"../../../openssl_1_1_1/libcrypto.a\",\"/lib/x86_64-linux-gnu/libdl.so.2\",\"/usr/lib/x86_64-linux-gnu/libpthread.so\"] \
        rtc_builtin_ssl_root_certificates=true \
        rtc_build_ffmpeg=false \
        rtc_ffmpeg_root=\"../../../ffmpeg\" \
        rtc_ffmpeg_libs=[\"../../../ffmpeg/libavcodec/libavcodec.a\",\"../../../ffmpeg/libswscale/libswscale.a\",\"../../../ffmpeg/libswresample/libswresample.a\",\"../../../ffmpeg/libavutil/libavutil.a\",\"/usr/local/lib/libva-x11.a\",\"/usr/local/lib/libva-drm.a\",\"/usr/local/lib/libva.a\",\"/usr/local/lib/libvdpau.a\",\"/usr/lib/x86_64-linux-gnu/libdrm.a\",\"/usr/lib/x86_64-linux-gnu/libXfixes.a\",\"/usr/lib/x86_64-linux-gnu/libXext.a\",\"/usr/lib/x86_64-linux-gnu/libX11.a\",\"/usr/lib/x86_64-linux-gnu/libxcb.a\",\"/usr/lib/x86_64-linux-gnu/libXau.a\",\"/usr/lib/x86_64-linux-gnu/libXdmcp.a\",\"/usr/lib/x86_64-linux-gnu/libdbus-1.a\"] \
        rtc_enable_protobuf=false`

    gn gen out/Debug --args="$ArgumentsList"
fi

# We should use bundled:
# rtc_build_opus=false
# rtc_include_opus=false
# rtc_include_pulse_audio=false

# Can we switch this off with bundled ssl?
# rtc_builtin_ssl_root_certificates=false

# For release build use:
# symbol_level=1
# is_debug=false
# rtc_use_gtk=false
# rtc_disable_logging=true
# enable_iterator_debugging=false
# rtc_disable_metrics?
# rtc_disable_trace_events?
# rtc_enable_external_auth?
# rtc_enable_sctp?
# rtc_exclude_audio_processing_module?
# rtc_include_builtin_audio_codecs?
# rtc_include_builtin_video_codecs?
# rtc_include_ilbc?
# rtc_include_internal_audio_device? Chromium doesn't use that
# rtc_opus_variable_complexity?
# rtc_use_h264?

echo Configuration done, now run "ninja -C out/Debug webrtc".
