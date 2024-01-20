// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "webrtc/webrtc_device_common.h"

namespace Webrtc {

class Environment;

class DeviceId final {
public:
	DeviceId(
		not_null<Environment*> environment,
		DeviceType type,
		rpl::producer<QString> savedValue);

	[[nodiscard]] QString current() const;
	[[nodiscard]] rpl::producer<QString> value() const;
	[[nodiscard]] rpl::producer<QString> changes() const;

	[[nodiscard]] DeviceChangeReason lastChangeReason() const;

private:
	void trackSavedId();

	const not_null<Environment*> _environment;
	const DeviceType _type;
	QString _savedId;
	rpl::variable<QString> _data;
	DeviceChangeReason _lastChangeReason = DeviceChangeReason::Manual;
	rpl::lifetime _lifetime;

};

[[nodiscard]] rpl::producer<QString> DeviceIdOrDefault(
	rpl::producer<QString> id);

[[nodiscard]] rpl::producer<QString> DeviceIdValueWithFallback(
	rpl::producer<QString> id,
	rpl::producer<QString> fallback);

} // namespace Webrtc
