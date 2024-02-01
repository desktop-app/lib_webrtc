// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "webrtc/platform/webrtc_platform_environment.h"

#include "webrtc/details/webrtc_environment_openal.h"
#include "webrtc/details/webrtc_environment_video_capture.h"

namespace Webrtc::Platform {

class EnvironmentLinux final : public Environment {
public:
	explicit EnvironmentLinux(not_null<EnvironmentDelegate*> delegate);
	~EnvironmentLinux();

	QString defaultId(DeviceType type) override;
	DeviceInfo device(DeviceType type, const QString &id) override;

	std::vector<DeviceInfo> devices(DeviceType type) override;
	bool refreshFullListOnChange(DeviceType type) override;

	bool desktopCaptureAllowed() const override;
	std::optional<QString> uniqueDesktopCaptureSource() const override;

	void defaultIdRequested(DeviceType type) override;
	void devicesRequested(DeviceType type) override;

	DeviceResolvedId threadSafeResolveId(
		const DeviceResolvedId &lastResolvedId,
		const QString &savedId) override;

private:
	details::EnvironmentOpenAL _audioFallback;
	details::EnvironmentVideoCapture _cameraFallback;
	bool _pipewireInitialized = false;

};

} // namespace Webrtc::Platform
