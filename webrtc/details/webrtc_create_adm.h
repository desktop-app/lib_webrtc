// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "modules/audio_device/include/audio_device.h"

namespace webrtc {
class TaskQueueFactory;
} // namespace webrtc

namespace Webrtc::details {

rtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModule(
	webrtc::TaskQueueFactory* task_queue_factory);

} // namespace Webrtc::details
