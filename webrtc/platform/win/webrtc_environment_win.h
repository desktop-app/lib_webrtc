// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "webrtc/platform/webrtc_platform_environment.h"

#include "webrtc/details/webrtc_environment_video_capture.h"
#include "base/platform/win/base_windows_winrt.h"

//#define WEBRTC_TESTING_OPENAL

#ifdef WEBRTC_TESTING_OPENAL
#include "webrtc/details/webrtc_environment_openal.h"
#endif // WEBRTC_TESTING_OPENAL

struct IMMDeviceEnumerator;
struct IMMNotificationClient;

namespace Webrtc::Platform {

class EnvironmentWin final : public Environment {
public:
	explicit EnvironmentWin(not_null<EnvironmentDelegate*> delegate);
	~EnvironmentWin();

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
	void processDeviceStateChange(
		const QString &id,
		std::optional<DeviceStateChange> change);

	class Client;

	const not_null<EnvironmentDelegate*> _delegate;
#ifdef WEBRTC_TESTING_OPENAL
	details::EnvironmentOpenAL _audioFallback;
#endif // WEBRTC_TESTING_OPENAL
	details::EnvironmentVideoCapture _cameraFallback;

	bool _comInitialized = false;
	winrt::com_ptr<IMMDeviceEnumerator> _enumerator;
	winrt::com_ptr<IMMNotificationClient> _client;

};

} // namespace Webrtc::Platform
