// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/details/webrtc_environment_openal.h"

#include <al.h>
#include <alc.h>

namespace Webrtc::details {
namespace {

template <typename Callback>
void EnumerateDevices(DeviceType type, Callback &&callback) {
	const auto specifiers = {
		ALC_DEVICE_SPECIFIER,
		(type == DeviceType::Playback)
			? ALC_ALL_DEVICES_SPECIFIER
			: ALC_CAPTURE_DEVICE_SPECIFIER,
	};
	for (const auto &specifier : specifiers) {
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
	Expects(type == DeviceType::Playback || type == DeviceType::Capture);

	return QString::fromUtf8(
		alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER));
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

} // namespace Webrtc::details
