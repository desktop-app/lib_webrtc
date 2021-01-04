// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <functional>

namespace webrtc {
class AudioDeviceModule;
class TaskQueueFactory;
}

namespace rtc {
template <class T>
class scoped_refptr;
}

namespace Webrtc {

using AudioDeviceModulePtr = rtc::scoped_refptr<webrtc::AudioDeviceModule>;
AudioDeviceModulePtr CreateAudioDeviceModule(
	webrtc::TaskQueueFactory* factory);

auto AudioDeviceModuleCreator()
-> std::function<AudioDeviceModulePtr(webrtc::TaskQueueFactory*)>;

} // namespace Webrtc
