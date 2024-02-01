// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/qt/qt_compare.h"
#include "webrtc/webrtc_device_common.h"

#include <QtCore/QMutex>

namespace Webrtc {

class Environment;

class DeviceResolver final {
public:
	DeviceResolver(
		not_null<Environment*> environment,
		DeviceType type,
		rpl::producer<QString> savedValue);

	[[nodiscard]] DeviceResolvedId current() const;
	[[nodiscard]] rpl::producer<DeviceResolvedId> value() const;
	[[nodiscard]] rpl::producer<DeviceResolvedId> changes() const;

	[[nodiscard]] DeviceResolvedId threadSafeCurrent() const;

	[[nodiscard]] DeviceChangeReason lastChangeReason() const;

private:
	void trackSavedId();

	const not_null<Environment*> _environment;
	QString _savedId;
	DeviceResolvedId _current;
	mutable QMutex _mutex;
	rpl::variable<DeviceResolvedId> _data;
	DeviceChangeReason _lastChangeReason = DeviceChangeReason::Manual;
	rpl::lifetime _lifetime;

};

[[nodiscard]] rpl::producer<QString> DeviceIdOrDefault(
	rpl::producer<QString> id);

[[nodiscard]] rpl::producer<QString> DeviceIdValueWithFallback(
	rpl::producer<QString> id,
	rpl::producer<QString> fallback);

} // namespace Webrtc
