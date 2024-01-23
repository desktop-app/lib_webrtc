// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "webrtc/platform/webrtc_platform_environment.h"

namespace Webrtc::Platform {

class EnvironmentMac final : public Environment {
public:
	explicit EnvironmentMac(not_null<EnvironmentDelegate*> delegate);
	~EnvironmentMac();

	QString defaultId(DeviceType type) override;
	DeviceInfo device(DeviceType type, const QString &id) override;

	std::vector<DeviceInfo> devices(DeviceType type) override;
	bool refreshFullListOnChange(DeviceType type) override;

	bool desktopCaptureAllowed() const override;
	std::optional<QString> uniqueDesktopCaptureSource() const override;

	void defaultPlaybackDeviceChanged();
	void defaultCaptureDeviceChanged();
	void audioDeviceListChanged();

private:
	const not_null<EnvironmentDelegate*> _delegate;

};

} // namespace Webrtc::Platform
