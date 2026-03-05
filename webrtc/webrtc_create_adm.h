// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <functional>
#include <memory>
#include <mutex>

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
[[nodiscard]] bool LoopbackAudioCaptureSupported();

auto LoopbackAudioDeviceModuleCreator()
-> std::function<AudioDeviceModulePtr(webrtc::TaskQueueFactory*)>;

namespace details {
class MixingAudioDeviceModule;
} // namespace details

class MixingAudioControl final {
public:
	void setLoopbackEnabled(bool enabled);
	[[nodiscard]] bool loopbackEnabled() const;

	void setMicrophoneMuted(bool muted);
	[[nodiscard]] bool microphoneMuted() const;

	void setPlaybackVolume(float volume);
	[[nodiscard]] float playbackVolume() const;

private:
	friend class details::MixingAudioDeviceModule;
	void attach(details::MixingAudioDeviceModule *module);
	void detach();

	std::mutex _mutex;
	details::MixingAudioDeviceModule *_module = nullptr;
	bool _pendingEnabled = false;
	bool _microphoneMuted = false;
	float _playbackVolume = 1.f;

};

auto MixingAudioDeviceModuleCreator(
	std::function<AudioDeviceModulePtr(webrtc::TaskQueueFactory*)>
		innerCreator,
	std::shared_ptr<MixingAudioControl> control)
-> std::function<AudioDeviceModulePtr(webrtc::TaskQueueFactory*)>;

} // namespace Webrtc
