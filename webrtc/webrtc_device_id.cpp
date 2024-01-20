// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_device_id.h"

#include "webrtc/webrtc_environment.h"

namespace Webrtc {
namespace {

[[nodiscard]] bool IsDefault(const QString &id) {
	return id.isEmpty() || id == kDefaultDeviceId;
}

} // namespace

DeviceId::DeviceId(
	not_null<Environment*> environment,
	DeviceType type,
	rpl::producer<QString> savedId)
: _environment(environment)
, _type(type) {
	std::move(
		savedId
	) | rpl::start_with_next([=](QString id) {
		_savedId = id;
		trackSavedId();
	}, _lifetime);
}

void DeviceId::trackSavedId() {
	const auto now = _environment->defaultId(_type);
	if (IsDefault(_savedId)) {
		_data = rpl::single(
			DeviceChange{ now, now }
		) | rpl::then(
			_environment->defaultChanges(_type)
		) | rpl::map([=](const DeviceChange &change) {
			_lastChangeReason = change.reason;
			return change.nowId;
		});
		return;
	}
	_data = rpl::single(DevicesChange{
		DeviceChange{ now, now },
		_environment->devices(_type),
	}) | rpl::then(
		_environment->changes(_type)
	) | rpl::map([=](const DevicesChange &change) {
		const auto now = _data.current();
		const auto i = ranges::find(
			change.nowList,
			_savedId,
			&DeviceInfo::id);
		if (i != end(change.nowList) && !i->inactive) {
			if (now != _savedId) {
				_lastChangeReason = DeviceChangeReason::Connected;
			}
			return _savedId;
		} else {
			_lastChangeReason = (now == _savedId)
				? DeviceChangeReason::Disconnected
				: change.defaultChange.reason;
			return change.defaultChange.nowId;
		}
	});
}

QString DeviceId::current() const {
	return _data.current();
}

rpl::producer<QString> DeviceId::value() const {
	return _data.changes();
}

rpl::producer<QString> DeviceId::changes() const {
	return _data.value();
}

DeviceChangeReason DeviceId::lastChangeReason() const {
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
