// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "webrtc/platform/webrtc_platform_environment.h"

namespace Webrtc::details {

class EnvironmentOpenAL final : public Platform::Environment {
public:
	using EnvironmentDelegate = Platform::EnvironmentDelegate;

	explicit EnvironmentOpenAL(not_null<EnvironmentDelegate*> delegate);
	~EnvironmentOpenAL();

	QString defaultId(DeviceType type) override;
	DeviceInfo device(DeviceType type, const QString &id) override;

	std::vector<DeviceInfo> devices(DeviceType type) override;
	bool refreshFullListOnChange(DeviceType type) override;

	bool desktopCaptureAllowed() const override;
	std::optional<QString> uniqueDesktopCaptureSource() const override;

private:
	const not_null<EnvironmentDelegate*> _delegate;

};

} // namespace Webrtc::details
