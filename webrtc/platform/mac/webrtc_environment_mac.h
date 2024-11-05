// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/weak_ptr.h"
#include "webrtc/platform/webrtc_platform_environment.h"

#include <media/engine/webrtc_media_engine.h>

namespace webrtc {
template <class T>
class scoped_refptr;
} // namespace webrtc

namespace rtc {
template <typename T>
using scoped_refptr = webrtc::scoped_refptr<T>;
} // namespace rtc

namespace webrtc {
class TaskQueueFactory;
class AudioDeviceModule;
} // namespace webrtc

namespace Webrtc::Platform {

class EnvironmentMac final : public Environment, public base::has_weak_ptr {
public:
	explicit EnvironmentMac(not_null<EnvironmentDelegate*> delegate);
	~EnvironmentMac();

	QString defaultId(DeviceType type) override;
	DeviceInfo device(DeviceType type, const QString &id) override;

	std::vector<DeviceInfo> devices(DeviceType type) override;
	bool refreshFullListOnChange(DeviceType type) override;

	bool desktopCaptureAllowed() const override;
	std::optional<QString> uniqueDesktopCaptureSource() const override;

	void defaultIdRequested(DeviceType type) override;
	void devicesRequested(DeviceType type) override;

	void setCaptureMuted(bool muted) override;
	void setCaptureMuteTracker(
		not_null<CaptureMuteTracker*> tracker,
		bool track) override;

	void defaultPlaybackDeviceChanged();
	void defaultCaptureDeviceChanged();
	void audioDeviceListChanged();

	[[nodiscard]] static QString DefaultId(DeviceType type);

private:
	void captureMuteSubscribe();
	void captureMuteUnsubscribe();
	void captureMuteRestartAdm();

	const not_null<EnvironmentDelegate*> _delegate;

	CaptureMuteTracker *_captureMuteTracker = nullptr;
	bool _captureMuteNotification = false;
	bool _captureMuted = false;

	std::unique_ptr<webrtc::TaskQueueFactory> _admTaskQueueFactory;
	rtc::scoped_refptr<webrtc::AudioDeviceModule> _adm;
	Fn<void(DeviceResolvedId)> _admSetDeviceIdCallback;
	DeviceResolvedId _admCaptureDeviceId;

	rpl::lifetime _captureMuteTrackerLifetime;
	rpl::lifetime _captureMuteSubscriptionLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Webrtc::Platform
