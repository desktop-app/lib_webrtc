// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_media_devices.h"

#undef emit

#include "webrtc/mac/webrtc_media_devices_mac.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "modules/video_capture/video_capture_factory.h"
#include "modules/audio_device/include/audio_device_factory.h"
#include "base/platform/base_platform_info.h"
#include "crl/crl_async.h"
//#include "media/engine/webrtc_media_engine.h"

namespace Webrtc {

std::vector<VideoInput> GetVideoInputList() {
#ifdef WEBRTC_MAC
	return MacGetVideoInputList();
#else // WEBRTC_MAC
	const auto info = std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo>(
		webrtc::VideoCaptureFactory::CreateDeviceInfo());
	auto result = std::vector<VideoInput>();
	if (!info) {
		return result;
	}
	const auto count = info->NumberOfDevices();
	for (auto i = uint32_t(); i != count; ++i) {
		constexpr auto kLengthLimit = 256;
		auto id = std::string(kLengthLimit, char(0));
		auto name = std::string(kLengthLimit, char(0));
		info->GetDeviceName(
			i,
			name.data(),
			name.size(),
			id.data(),
			id.size());
		result.push_back({
			.id = QString::fromUtf8(id.c_str()),
			.name = QString::fromUtf8(name.c_str())
		});
	}
	return result;
#endif // WEBRTC_MAC
}

std::vector<AudioInput> GetAudioInputList() {
	auto result = std::vector<AudioInput>();
	const auto resolve = [&] {
		const auto queueFactory = webrtc::CreateDefaultTaskQueueFactory();
		const auto info = webrtc::AudioDeviceModule::Create(
			webrtc::AudioDeviceModule::kPlatformDefaultAudio,
			queueFactory.get());
		if (!info || info->Init() < 0) {
			return;
		}
		const auto count = info->RecordingDevices();
		if (count <= 0) {
			return;
		}
		for (auto i = int16_t(); i != count; ++i) {
			char name[webrtc::kAdmMaxDeviceNameSize + 1] = { 0 };
			char id[webrtc::kAdmMaxGuidSize + 1] = { 0 };
			info->RecordingDeviceName(i, name, id);
			result.push_back({
				.id = QString::fromUtf8(id),
				.name = QString::fromUtf8(name)
			});
		}
	};
	if constexpr (Platform::IsWindows()) {
		// Windows version requires MultiThreaded COM apartment.
		crl::sync(resolve);
	} else {
		resolve();
	}
	return result;
}

std::vector<AudioOutput> GetAudioOutputList() {
	auto result = std::vector<AudioOutput>();
	const auto resolve = [&] {
		const auto queueFactory = webrtc::CreateDefaultTaskQueueFactory();
		const auto info = webrtc::AudioDeviceModule::Create(
			webrtc::AudioDeviceModule::kPlatformDefaultAudio,
			queueFactory.get());
		if (!info || info->Init() < 0) {
			return;
		}
		const auto count = info->PlayoutDevices();
		if (count <= 0) {
			return;
		}
		for (auto i = int16_t(); i != count; ++i) {
			char name[webrtc::kAdmMaxDeviceNameSize + 1] = { 0 };
			char id[webrtc::kAdmMaxGuidSize + 1] = { 0 };
			info->PlayoutDeviceName(i, name, id);
			result.push_back({
				.id = QString::fromUtf8(id),
				.name = QString::fromUtf8(name)
			});
		}
	};
	if constexpr (Platform::IsWindows()) {
		// Windows version requires MultiThreaded COM apartment.
		crl::sync(resolve);
	} else {
		resolve();
	}
	return result;
}

} // namespace Webrtc
