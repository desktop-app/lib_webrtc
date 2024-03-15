// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "webrtc/webrtc_device_common.h"

#include <rpl/producer.h>
#include <optional>

namespace Webrtc::Platform {

class Environment;

class EnvironmentDelegate {
public:
	virtual void defaultChanged(
		DeviceType type,
		DeviceChangeReason reason,
		QString nowId) = 0;
	virtual void deviceStateChanged(
		DeviceType type,
		QString id,
		DeviceStateChange state) = 0;
	virtual void devicesForceRefresh(DeviceType type) = 0;
};

} // namespace Webrtc::Platform

namespace Webrtc {

struct DeviceId;

class Environment final : private Platform::EnvironmentDelegate {
public:
	Environment();
	~Environment();

	void forceRefresh(DeviceType type);
	[[nodiscard]] QString defaultId(DeviceType type) const;
	[[nodiscard]] std::vector<DeviceInfo> devices(DeviceType type) const;
	[[nodiscard]] rpl::producer<DevicesChange> changes(
		DeviceType type) const;
	[[nodiscard]] rpl::producer<DeviceChange> defaultChanges(
		DeviceType type) const;
	[[nodiscard]] rpl::producer<std::vector<DeviceInfo>> devicesValue(
		DeviceType type) const;

	void validateDefaultId(DeviceType type) const;
	void validateDevices(DeviceType type) const;

	[[nodiscard]] bool desktopCaptureAllowed() const;
	[[nodiscard]] std::optional<QString> uniqueDesktopCaptureSource() const;

	[[nodiscard]] DeviceResolvedId threadSafeResolveId(
		const DeviceResolvedId &lastResolvedId,
		const QString &savedId) const;

	void setCaptureMuted(bool muted);
	void setCaptureMuteTracker(
		not_null<CaptureMuteTracker*> tracker,
		bool track);

private:
	static constexpr auto kTypeCount = 3;

	enum class LogType : uchar {
		Initial,
		Always,
		Debug,
	};

	struct DevicesChangeEvent {
		DeviceChange defaultChange;
		bool listChanged = false;
	};

	struct Devices {
		QString defaultId;
		rpl::event_stream<DevicesChangeEvent> changes;

		std::vector<DeviceInfo> list;
		rpl::event_stream<> listChanges;

		std::optional<QString> defaultChangeFrom;
		DeviceChangeReason defaultChangeReason = DeviceChangeReason::Manual;
		bool refreshFullListOnChange = false;
		bool listChanged = false;
	};

	[[nodiscard]] static int TypeToIndex(DeviceType type);

	[[nodiscard]] Devices resolveDevices(DeviceType type) const;
	[[nodiscard]] bool synced(DeviceType type) const;
	void validateAfterDefaultChange(DeviceType type);
	void validateAfterListChange(DeviceType type);
	void refreshDevices(DeviceType type);
	void maybeNotify(DeviceType type);
	void logSyncError(DeviceType type);
	void logState(DeviceType type, LogType log);

	void defaultChanged(
		DeviceType type,
		DeviceChangeReason reason,
		QString nowId) override;
	void deviceStateChanged(
		DeviceType type,
		QString id,
		DeviceStateChange state) override;
	void devicesForceRefresh(DeviceType type) override;

	const std::unique_ptr<Platform::Environment> _platform;

	std::array<Devices, kTypeCount> _devices;

	rpl::lifetime _lifetime;

};

} // namespace Webrtc
