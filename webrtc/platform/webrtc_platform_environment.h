// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "webrtc/webrtc_device_common.h"

#include <optional>

namespace Webrtc::Platform {

class EnvironmentDelegate;

class Environment {
public:
	virtual ~Environment() = default;

	[[nodiscard]] virtual QString defaultId(DeviceType type) = 0;
	[[nodiscard]] virtual DeviceInfo device(
		DeviceType type,
		const QString &id) = 0;

	[[nodiscard]] virtual std::vector<DeviceInfo> devices(
		DeviceType type) = 0;
	[[nodiscard]] virtual bool refreshFullListOnChange(
		DeviceType type) = 0;

	[[nodiscard]] virtual bool desktopCaptureAllowed() const = 0;
	[[nodiscard]] virtual auto uniqueDesktopCaptureSource() const
		-> std::optional<QString> = 0;

	virtual void defaultIdRequested(DeviceType type) = 0;
	virtual void devicesRequested(DeviceType type) = 0;

	[[nodiscard]] virtual DeviceResolvedId threadSafeResolveId(
			const DeviceResolvedId &lastResolvedId,
			const QString &savedId) {
		return lastResolvedId;
	}

};

[[nodiscard]] std::unique_ptr<Environment> CreateEnvironment(
	not_null<EnvironmentDelegate*> delegate);

} // namespace Webrtc::Platform
