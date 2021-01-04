// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/details/webrtc_openal_adm.h"

#include "webrtc/details/webrtc_openal_input.h"

#include "modules/audio_device/win/audio_device_module_win.h"

#ifdef WEBRTC_WIN
#include <winsock2.h> // Otherwise: ws2def.h 'AF_IPX': macro redefinition.
#include "modules/audio_device/win/core_audio_input_win.h"
#include "modules/audio_device/win/core_audio_output_win.h"
#endif // WEBRTC_WIN

namespace Webrtc::details {

rtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModuleOpenAL(
		webrtc::TaskQueueFactory* taskQueueFactory) {
#ifdef WEBRTC_WIN
	using namespace webrtc::webrtc_win;
	return CreateWindowsCoreAudioAudioDeviceModuleFromInputAndOutput(
		std::make_unique<details::AudioInputOpenAL>(),
		std::make_unique<CoreAudioOutput>(true),
		taskQueueFactory);
#else // WEBRTC_WIN
	return nullptr;
#endif // WEBRTC_WIN
}

} // namespace Webrtc::details
