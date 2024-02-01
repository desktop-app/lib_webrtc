// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/details/webrtc_environment_openal.h"

#include "webrtc/webrtc_environment.h"

#include <al.h>
#include <alc.h>

namespace Webrtc::details {
namespace {

template <typename Callback>
void EnumerateDevices(DeviceType type, Callback &&callback) {
	const auto specifier = (type == DeviceType::Playback)
		? ALC_ALL_DEVICES_SPECIFIER
		: ALC_CAPTURE_DEVICE_SPECIFIER;
	auto devices = alcGetString(nullptr, specifier);
	Assert(devices != nullptr);
	while (*devices != 0) {
		callback(devices);
		while (*devices != 0) {
			++devices;
		}
		++devices;
	}
}

[[nodiscard]] DeviceInfo DeviceFromOpenAL(
	DeviceType type,
	const char *device) {
	if (!device) {
		return {};
	}
	const auto guid = QString::fromUtf8(device);
	const auto prefix = u"OpenAL Soft on "_q;
	return {
		.id = guid,
		.name = (guid.startsWith(prefix) ? guid.mid(prefix.size()) : guid),
		.type = type,
	};
}

} // namespace

EnvironmentOpenAL::EnvironmentOpenAL(not_null<EnvironmentDelegate*> delegate)
: _delegate(delegate) {
}

EnvironmentOpenAL::~EnvironmentOpenAL() {
}

QString EnvironmentOpenAL::defaultId(DeviceType type) {
	return DefaultId(type);
}

QString EnvironmentOpenAL::DefaultId(DeviceType type) {
	Expects(type == DeviceType::Playback || type == DeviceType::Capture);

	[[maybe_unused]] const auto reenumerate = alcGetString(
		nullptr,
		(type == DeviceType::Capture)
			? ALC_CAPTURE_DEVICE_SPECIFIER
			: ALC_ALL_DEVICES_SPECIFIER);

	return QString::fromUtf8(
		alcGetString(nullptr, (type == DeviceType::Capture)
			? ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER
			: ALC_DEFAULT_ALL_DEVICES_SPECIFIER));
}

DeviceResolvedId EnvironmentOpenAL::DefaultResolvedId(DeviceType type) {
	return { DefaultId(type), type, true };
}

DeviceResolvedId EnvironmentOpenAL::ResolveId(
		DeviceType type,
		const QString &savedId) {
	if (savedId.isEmpty() || savedId == kDefaultDeviceId) {
		return DefaultResolvedId(type);
	}
	auto found = false;
	EnumerateDevices(type, [&](const char *device) {
		const auto info = DeviceFromOpenAL(type, device);
		if (info.id == savedId) {
			found = true;
		}
	});
	return found
		? DeviceResolvedId{ savedId, type }
		: DefaultResolvedId(type);
}

DeviceInfo EnvironmentOpenAL::device(DeviceType type, const QString &id) {
	Expects(type == DeviceType::Playback || type == DeviceType::Capture);

	auto result = DeviceInfo();
	EnumerateDevices(type, [&](const char *device) {
		auto info = DeviceFromOpenAL(type, device);
		if (info.id == id) {
			result = std::move(info);
		}
	});
	return result;
}

std::vector<DeviceInfo> EnvironmentOpenAL::devices(DeviceType type) {
	Expects(type == DeviceType::Playback || type == DeviceType::Capture);

	auto result = std::vector<DeviceInfo>();
	EnumerateDevices(type, [&](const char *device) {
		if (auto info = DeviceFromOpenAL(type, device)) {
			result.push_back(std::move(info));
		}
	});
	return result;
}

bool EnvironmentOpenAL::refreshFullListOnChange(DeviceType type) {
	Expects(type == DeviceType::Playback || type == DeviceType::Capture);

	return true;
}

bool EnvironmentOpenAL::desktopCaptureAllowed() const {
	Unexpected("EnvironmentOpenAL::desktopCaptureAllowed.");
}

auto EnvironmentOpenAL::uniqueDesktopCaptureSource() const
-> std::optional<QString> {
	Unexpected("EnvironmentOpenAL::uniqueDesktopCaptureSource.");
}

void EnvironmentOpenAL::defaultIdRequested(DeviceType type) {
	_delegate->devicesForceRefresh(type);
}

void EnvironmentOpenAL::devicesRequested(DeviceType type) {
	_delegate->devicesForceRefresh(type);
}

DeviceResolvedId EnvironmentOpenAL::threadSafeResolveId(
		const DeviceResolvedId &lastResolvedId,
		const QString &savedId) {
	const auto result = ResolveId(lastResolvedId.type, savedId);
	if (result != lastResolvedId) {
		crl::on_main(this, [=, type = lastResolvedId.type] {
			_delegate->devicesForceRefresh(type);
		});
	}
	return result;
}

} // namespace Webrtc::details
