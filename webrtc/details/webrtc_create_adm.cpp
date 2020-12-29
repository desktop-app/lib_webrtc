// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/details/webrtc_create_adm.h"

#include "modules/audio_device/include/audio_device_factory.h"

namespace Webrtc::details {

rtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModule(
		webrtc::TaskQueueFactory* task_queue_factory) {
	const auto check = [&](webrtc::AudioDeviceModule::AudioLayer layer) {
		auto result = webrtc::AudioDeviceModule::Create(
			layer,
			task_queue_factory);
		return (result && (result->Init() == 0)) ? result : nullptr;
	};
#ifdef WEBRTC_WIN
	if (auto result = webrtc::CreateWindowsCoreAudioAudioDeviceModule(
			task_queue_factory)) {
		if (result->Init() == 0) {
			return result;
		}
	}
#endif // WEBRTC_WIN
	if (auto result = check(webrtc::AudioDeviceModule::kPlatformDefaultAudio)) {
		return result;
#ifdef WEBRTC_LINUX
	} else if (auto result = check(webrtc::AudioDeviceModule::kLinuxAlsaAudio)) {
		return result;
#endif // WEBRTC_LINUX
	}
	return nullptr;
}

} // namespace Webrtc::details
