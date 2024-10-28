// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/weak_ptr.h"
#include "webrtc/platform/webrtc_platform_environment.h"

namespace Webrtc::details {

class EnvironmentOpenAL final
	: public Platform::Environment
	, public base::has_weak_ptr {
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

	void defaultIdRequested(DeviceType type) override;
	void devicesRequested(DeviceType type) override;

	DeviceResolvedId threadSafeResolveId(
		const DeviceResolvedId &lastResolvedId,
		const QString &savedId) override;

	[[nodiscard]] static QString DefaultId(DeviceType type);

private:
	[[nodiscard]] static DeviceResolvedId DefaultResolvedId(DeviceType type);
	[[nodiscard]] static DeviceResolvedId ResolveId(
		DeviceType type,
		const QString &savedId);

	const not_null<EnvironmentDelegate*> _delegate;

};

} // namespace Webrtc::details
