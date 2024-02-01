// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_create_adm.h"

#include "webrtc/details/webrtc_openal_adm.h"

#include <api/make_ref_counted.h>
#include <modules/audio_device/include/audio_device_factory.h>

#ifdef WEBRTC_WIN
#include "webrtc/platform/win/webrtc_loopback_adm_win.h"
#endif // WEBRTC_WIN

namespace Webrtc {

rtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModule(
		webrtc::TaskQueueFactory *factory,
		Fn<void(Fn<void(DeviceResolvedId)>)> saveSetDeviceIdCallback) {
	auto result = rtc::make_ref_counted<details::AudioDeviceOpenAL>(factory);
	if (!result || result->Init() != 0) {
		return nullptr;
	}
	saveSetDeviceIdCallback(result->setDeviceIdCallback());
	return result;
}

auto AudioDeviceModuleCreator(
	Fn<void(Fn<void(DeviceResolvedId)>)> saveSetDeviceIdCallback)
-> std::function<AudioDeviceModulePtr(webrtc::TaskQueueFactory*)> {
	return [=](webrtc::TaskQueueFactory *factory) {
		return CreateAudioDeviceModule(factory, saveSetDeviceIdCallback);
	};
}

AudioDeviceModulePtr CreateLoopbackAudioDeviceModule(
		webrtc::TaskQueueFactory* factory) {
#ifdef WEBRTC_WIN
	auto result = rtc::make_ref_counted<details::AudioDeviceLoopbackWin>(
		factory);
	if (result->Init() == 0) {
		return result;
	}
#endif // WEBRTC_WIN
	return nullptr;
}

auto LoopbackAudioDeviceModuleCreator()
-> std::function<AudioDeviceModulePtr(webrtc::TaskQueueFactory*)> {
	return CreateLoopbackAudioDeviceModule;
}

} // namespace Webrtc
