// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_environment.h"

#include "base/debug_log.h"
#include "webrtc/platform/webrtc_platform_environment.h"

namespace Webrtc {
namespace {

[[nodiscard]] QString SerializeDevices(const std::vector<DeviceInfo> &list) {
	auto result = QStringList();
	for (const auto &device : list) {
		result.push_back('"' + device.name + "\" <" + device.id + ">");
	}
	return "{ " + result.join(", ") + " }";
}

[[nodiscard]] QString TypeToString(DeviceType type) {
	switch (type) {
	case DeviceType::Playback: return "Playback";
	case DeviceType::Capture: return "Capture";
	case DeviceType::Camera: return "Camera";
	}
	Unexpected("Type in TypeToString.");
}

} // namespace

Environment::Environment()
: _platform(
	Platform::CreateEnvironment((Platform::EnvironmentDelegate*)this))
, _devices{
	Devices{
		.defaultId = _platform->defaultId(DeviceType::Playback),
		.list = _platform->devices(DeviceType::Playback),
	},
	Devices{
		.defaultId = _platform->defaultId(DeviceType::Capture),
		.list = _platform->devices(DeviceType::Capture),
	},
	Devices{
		.defaultId = _platform->defaultId(DeviceType::Camera),
		.list = _platform->devices(DeviceType::Camera),
	},
} {
	using Type = DeviceType;
	for (const auto type : { Type::Playback, Type::Capture, Type::Camera }) {
		if (synced(type)) {
			logState(type, LogType::Initial);
		} else {
			logSyncError(type);
		}
	}
}

Environment::~Environment() = default;

int Environment::TypeToIndex(DeviceType type) {
	const auto result = int(type);

	Ensures(result >= 0 && result < kTypeCount);
	return result;
}

QString Environment::defaultId(DeviceType type) const {
	return _devices[TypeToIndex(type)].defaultId;
}

std::vector<DeviceInfo> Environment::devices(DeviceType type) const {
	return _devices[TypeToIndex(type)].list;
}

rpl::producer<DevicesChange> Environment::changes(
		DeviceType type) const {
	const auto devices = &_devices[TypeToIndex(type)];
	return devices->changes.events(
	) | rpl::map([=](DevicesChangeEvent &&event) -> DevicesChange {
		return { std::move(event.defaultChange), devices->list };
	});
}

rpl::producer<DeviceChange> Environment::defaultChanges(
		DeviceType type) const {
	return _devices[TypeToIndex(type)].changes.events(
	) | rpl::filter([](const DevicesChangeEvent &event) {
		return !!event.defaultChange;
	}) | rpl::map([](DevicesChangeEvent &&event) {
		return std::move(event.defaultChange);
	});
}

rpl::producer<std::vector<DeviceInfo>> Environment::devicesValue(
		DeviceType type) const {
	const auto devices = &_devices[TypeToIndex(type)];
	return devices->changes.events_starting_with(
		DevicesChangeEvent{ .listChanged = true }
	) | rpl::filter([](const DevicesChangeEvent &event) {
		return event.listChanged;
	}) | rpl::map([=] {
		return devices->list;
	});
}

bool Environment::desktopCaptureAllowed() const {
	return _platform->desktopCaptureAllowed();
}

std::optional<QString> Environment::uniqueDesktopCaptureSource() const {
	return _platform->uniqueDesktopCaptureSource();
}

void Environment::defaultChanged(
		DeviceType type,
		DeviceChangeReason reason,
		QString nowId) {
	auto &devices = _devices[TypeToIndex(type)];
	devices.defaultChangeFrom = std::exchange(
		devices.defaultId,
		std::move(nowId));
	devices.defaultChangeReason = DeviceChangeReason::Manual;
	validateAfterDefaultChange(type);
	maybeNotify(type);
}

void Environment::deviceStateChanged(
		DeviceType type,
		QString id,
		DeviceStateChange state) {
	auto &devices = _devices[TypeToIndex(type)];
	if (devices.refreshFullListOnChange) {
		refreshDevices(type);
	}
	const auto i = ranges::find(
		devices.list,
		id,
		&DeviceInfo::id);
	if (i == end(devices.list)) {
		if (state == DeviceStateChange::Disconnected) {
			return;
		} else if (auto info = _platform->device(type, id)) {
			devices.list.push_back(std::move(info));
			devices.listChanged = true;
		}
	} else if (state == DeviceStateChange::Disconnected) {
		const auto from = ranges::remove(
			i,
			end(devices.list),
			id,
			&DeviceInfo::id);
		if (from != end(devices.list)) {
			devices.list.erase(from, end(devices.list));
			devices.listChanged = true;
		}
	} else if (i != end(devices.list)) {
		const auto inactive = (state != DeviceStateChange::Active);
		if (i->inactive != inactive) {
			i->inactive = inactive;
			devices.listChanged = true;
		}
	}
	validateAfterListChange(type);
	maybeNotify(type);
}

bool Environment::synced(DeviceType type) const {
	const auto &devices = _devices[TypeToIndex(type)];
	return ranges::contains(
		devices.list,
		devices.defaultId,
		&DeviceInfo::id);
}

void Environment::validateAfterListChange(DeviceType type) {
	auto &devices = _devices[TypeToIndex(type)];
	if (!devices.listChanged || synced(type)) {
		return;
	}
	devices.defaultChangeFrom = std::exchange(
		devices.defaultId,
		_platform->defaultId(type));
	devices.defaultChangeReason = DeviceChangeReason::Disconnected;
	if (devices.defaultChangeFrom != devices.defaultId
		&& synced(type)) {
		return;
	}
	refreshDevices(type);
	if (!devices.listChanged || !synced(type)) {
		logSyncError(type);
	}
}

void Environment::validateAfterDefaultChange(DeviceType type) {
	auto &devices = _devices[TypeToIndex(type)];
	if (!devices.defaultChangeFrom
		|| devices.defaultChangeFrom == devices.defaultId
		|| synced(type)) {
		return;
	}
	refreshDevices(type);
	if (devices.listChanged && synced(type)) {
		return;
	}
	auto changedOneMoreFromId = std::exchange(
		devices.defaultId,
		_platform->defaultId(type));
	devices.defaultChangeReason = DeviceChangeReason::Disconnected;
	if (devices.defaultId == changedOneMoreFromId || !synced(type)) {
		logSyncError(type);
	}
}

void Environment::maybeNotify(DeviceType type) {
	auto &devices = _devices[TypeToIndex(type)];
	if (devices.defaultChangeFrom == devices.defaultId) {
		devices.defaultChangeFrom = std::nullopt;
	}
	if (!devices.listChanged && !devices.defaultChangeFrom) {
		return;
	}
	const auto listChanged = base::take(devices.listChanged);
	const auto from = base::take(devices.defaultChangeFrom);
	const auto reason = base::take(devices.defaultChangeReason);
	devices.changes.fire({
		.defaultChange = {
			.wasId = from.value_or(QString()),
			.nowId = from ? devices.defaultId : QString(),
			.reason = (from ? reason : DeviceChangeReason::Manual),
		},
		.listChanged = listChanged,
	});
}

void Environment::logSyncError(DeviceType type) {
	auto &devices = _devices[TypeToIndex(type)];
	LOG(("Media Error: "
		"Can't sync default device for type %1, default: %2, list: %3"
		).arg(TypeToString(type)
		).arg(devices.defaultId
		).arg(SerializeDevices(devices.list)));
}

void Environment::logState(DeviceType type, LogType log) {
	auto &devices = _devices[TypeToIndex(type)];
	auto phrase = u"Media Info: Type %1, default: %2, list: %3"_q
		.arg(TypeToString(type))
		.arg(devices.defaultId)
		.arg(SerializeDevices(devices.list));
	if (log == LogType::Initial) {
		phrase += u", full list refresh: %1"_q.arg(
			devices.refreshFullListOnChange ? "true" : "false");
	}
	switch (log) {
	case LogType::Initial:
	case LogType::Always:
		LOG((phrase));
		break;
	case LogType::Debug:
		DEBUG_LOG((phrase));
		break;
	}
}

void Environment::refreshDevices(DeviceType type) {
	auto &devices = _devices[TypeToIndex(type)];
	auto list = _platform->devices(type);
	if (devices.list != list) {
		devices.list = std::move(list);
		devices.listChanged = true;
	}
}

} // namespace Webrtc
