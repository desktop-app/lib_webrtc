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
template <class T>
class scoped_refptr;
} // namespace webrtc

namespace rtc {
template <typename T>
using scoped_refptr = webrtc::scoped_refptr<T>;
} // namespace rtc

namespace Webrtc {

struct DeviceResolvedId;

using AudioDeviceModulePtr = rtc::scoped_refptr<webrtc::AudioDeviceModule>;
AudioDeviceModulePtr CreateAudioDeviceModule(
	webrtc::TaskQueueFactory* factory,
	Fn<void(Fn<void(DeviceResolvedId)>)> saveSetDeviceIdCallback);

auto AudioDeviceModuleCreator(
	Fn<void(Fn<void(DeviceResolvedId)>)> saveSetDeviceIdCallback)
-> std::function<AudioDeviceModulePtr(webrtc::TaskQueueFactory*)>;

AudioDeviceModulePtr CreateLoopbackAudioDeviceModule(
	webrtc::TaskQueueFactory* factory);

auto LoopbackAudioDeviceModuleCreator()
-> std::function<AudioDeviceModulePtr(webrtc::TaskQueueFactory*)>;

} // namespace Webrtc
