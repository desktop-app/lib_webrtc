// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_device_resolver.h"

#include "webrtc/webrtc_environment.h"

namespace Webrtc {
namespace {

[[nodiscard]] bool IsDefault(const QString &id) {
	return id.isEmpty() || id == kDefaultDeviceId;
}

} // namespace

DeviceResolver::DeviceResolver(
	not_null<Environment*> environment,
	DeviceType type,
	rpl::producer<QString> savedId)
: _environment(environment)
, _current{ .type = type }
, _data(_current) {
	_data.changes() | rpl::start_with_next([=](DeviceResolvedId id) {
		QMutexLocker lock(&_mutex);
		_current = id;
	}, _lifetime);

	std::move(
		savedId
	) | rpl::start_with_next([=](QString id) {
		QMutexLocker lock(&_mutex);
		_savedId = id;
		lock.unlock();

		trackSavedId();
	}, _lifetime);
}

void DeviceResolver::trackSavedId() {
	const auto now = _environment->defaultId(_current.type);
	if (IsDefault(_savedId)) {
		_data = rpl::single(
			DeviceChange{ now, now }
		) | rpl::then(
			_environment->defaultChanges(_current.type)
		) | rpl::map([=](const DeviceChange &change) {
			_lastChangeReason = change.reason;
			return DeviceResolvedId{ change.nowId, _current.type, true };
		});
		return;
	}
	_data = rpl::single(DevicesChange{
		DeviceChange{ now, now },
		_environment->devices(_current.type),
	}) | rpl::then(
		_environment->changes(_current.type)
	) | rpl::map([=](const DevicesChange &change) {
		const auto now = _data.current();
		const auto i = ranges::find(
			change.nowList,
			_savedId,
			&DeviceInfo::id);
		if (i != end(change.nowList) && !i->inactive) {
			auto result = DeviceResolvedId{ _savedId, now.type };
			if (now != result) {
				_lastChangeReason = DeviceChangeReason::Connected;
			}
			return result;
		} else {
			_lastChangeReason = (now == DeviceResolvedId{ _savedId })
				? DeviceChangeReason::Disconnected
				: change.defaultChange.reason;
			const auto &defaultId = change.defaultChange.nowId;
			return DeviceResolvedId{ defaultId, now.type, true };
		}
	});
}

DeviceResolvedId DeviceResolver::current() const {
	if (IsDefault(_savedId)) {
		_environment->validateDefaultId(_current.type);
	} else {
		_environment->validateDevices(_current.type);
	}

	return _data.current();
}

DeviceResolvedId DeviceResolver::threadSafeCurrent() const {
	QMutexLocker lock(&_mutex);
	auto savedId = _savedId;
	auto current = _current;
	lock.unlock();

	return _environment->threadSafeResolveId(current, savedId);
}

rpl::producer<DeviceResolvedId> DeviceResolver::value() const {
	return _data.value();
}

rpl::producer<DeviceResolvedId> DeviceResolver::changes() const {
	return _data.changes();
}

DeviceChangeReason DeviceResolver::lastChangeReason() const {
	return _lastChangeReason;
}

rpl::producer<QString> DeviceIdOrDefault(
		rpl::producer<QString> id) {
	return std::move(id) | rpl::map([](const QString &id) {
		return !id.isEmpty() ? id : kDefaultDeviceId;
	}) | rpl::distinct_until_changed();
}

rpl::producer<QString> DeviceIdValueWithFallback(
		rpl::producer<QString> id,
		rpl::producer<QString> fallback) {
	return rpl::combine(
		std::move(id),
		std::move(fallback)
	) | rpl::map([](const QString &id, const QString &fallback) {
		return !id.isEmpty()
			? id
			: !fallback.isEmpty()
			? fallback
			: kDefaultDeviceId;
	}) | rpl::distinct_until_changed();
}

} // namespace Webrtc
