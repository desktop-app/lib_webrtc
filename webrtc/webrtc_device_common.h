// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QString>

namespace Webrtc {

enum class DeviceType : uchar {
	Playback,
	Capture,
	Camera,
};

enum class DeviceStateChange : uchar {
	Active,
	Inactive,
	Disconnected,
};

enum class DeviceChangeReason : uchar {
	Manual,
	Connected,
	Disconnected,
};

struct DeviceInfo {
	QString id;
	QString name;
	DeviceType type = DeviceType::Playback;
	bool inactive = false;

	explicit operator bool() const {
		return !id.isEmpty();
	}
	friend inline bool operator==(
		const DeviceInfo &a,
		const DeviceInfo &b) = default;
};

struct DeviceChange {
	QString wasId;
	QString nowId;
	DeviceChangeReason reason = DeviceChangeReason::Manual;

	explicit operator bool() const {
		return wasId != nowId;
	}
	friend inline bool operator==(
		const DeviceChange &a,
		const DeviceChange &b) = default;
};

struct DevicesChange {
	DeviceChange defaultChange;
	std::vector<DeviceInfo> nowList;
};

inline QString kDefaultDeviceId = u"default"_q;

} // namespace Webrtc
