// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/platform/linux/webrtc_environment_linux.h"

#include "base/debug_log.h"

#ifdef WEBRTC_USE_PIPEWIRE
#include <modules/desktop_capture/linux/wayland/base_capturer_pipewire.h>
#endif // WEBRTC_USE_PIPEWIRE

namespace Webrtc::Platform {

EnvironmentLinux::EnvironmentLinux(not_null<EnvironmentDelegate*> delegate)
: _audioFallback(delegate)
, _cameraFallback(delegate) {
#ifdef WEBRTC_USE_PIPEWIRE
	if (!webrtc::BaseCapturerPipeWire::IsSupported()) {
		LOG(("Audio Info: Failed to load pipewire 0.3 stubs."));
	}
#endif // WEBRTC_USE_PIPEWIRE
}

EnvironmentLinux::~EnvironmentLinux() {
}

QString EnvironmentLinux::defaultId(DeviceType type) {
	return (type == DeviceType::Camera)
		? _cameraFallback.defaultId(type)
		: _audioFallback.defaultId(type);
}

DeviceInfo EnvironmentLinux::device(DeviceType type, const QString &id) {
	return (type == DeviceType::Camera)
		? _cameraFallback.device(type, id)
		: _audioFallback.device(type, id);
}

std::vector<DeviceInfo> EnvironmentLinux::devices(DeviceType type) {
	return (type == DeviceType::Camera)
		? _cameraFallback.devices(type)
		: _audioFallback.devices(type);
}

bool EnvironmentLinux::refreshFullListOnChange(DeviceType type) {
	return (type == DeviceType::Camera)
		? _cameraFallback.refreshFullListOnChange(type)
		: _audioFallback.refreshFullListOnChange(type);
}

bool EnvironmentLinux::desktopCaptureAllowed() const {
	return true;
}

std::optional<QString> EnvironmentLinux::uniqueDesktopCaptureSource() const {
#ifdef WEBRTC_USE_PIPEWIRE
	if (webrtc::DesktopCapturer::IsRunningUnderWayland()) {
		return u"desktop_capturer_pipewire"_q;
	}
#endif // WEBRTC_USE_PIPEWIRE
	return std::nullopt;
}

void EnvironmentLinux::defaultIdRequested(DeviceType type) {
	if (type == DeviceType::Camera) {
		_cameraFallback.defaultIdRequested(type);
	} else {
		_audioFallback.defaultIdRequested(type);
	}
}

void EnvironmentLinux::devicesRequested(DeviceType type) {
	if (type == DeviceType::Camera) {
		_cameraFallback.devicesRequested(type);
	} else {
		_audioFallback.devicesRequested(type);
	}
}

DeviceResolvedId EnvironmentLinux::threadSafeResolveId(
		const DeviceResolvedId &lastResolvedId,
		const QString &savedId) {
	return (lastResolvedId.type == DeviceType::Camera)
		? _cameraFallback.threadSafeResolveId(lastResolvedId, savedId)
		: _audioFallback.threadSafeResolveId(lastResolvedId, savedId);
}

std::unique_ptr<Environment> CreateEnvironment(
		not_null<EnvironmentDelegate*> delegate) {
	return std::make_unique<EnvironmentLinux>(delegate);
}

} // namespace Webrtc::Platform
