// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace webrtc {
class AudioDeviceModule;
class TaskQueueFactory;
}

namespace rtc {
template <class T>
class scoped_refptr;
}

namespace Webrtc::details {

rtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModuleOpenAL(
	webrtc::TaskQueueFactory *taskQueueFactory);

} // namespace Webrtc::details
