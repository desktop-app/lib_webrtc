// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "modules/audio_device/win/audio_device_module_win.h"

#include <al.h>
#include <alc.h>

namespace rtc {
class Thread;
} // namespace rtc

namespace webrtc {
class FineAudioBuffer;
} // namespace webrtc

namespace Webrtc::details {

class AudioInputOpenAL final : public webrtc::webrtc_win::AudioInput {
public:
	AudioInputOpenAL();
	~AudioInputOpenAL();

	int Init() override;
	int Terminate() override;
	int NumDevices() const override;
	int SetDevice(int index) override;
	int SetDevice(
		webrtc::AudioDeviceModule::WindowsDeviceType device) override;
	int DeviceName(int index, std::string *name, std::string *guid) override;
	void AttachAudioBuffer(webrtc::AudioDeviceBuffer *audio_buffer) override;
	bool RecordingIsInitialized() const override;
	int InitRecording() override;
	int StartRecording() override;
	int StopRecording() override;
	bool Recording() override;
	int VolumeIsAvailable(bool* available) override;
	int RestartRecording() override;
	bool Restarting() const override;
	int SetSampleRate(uint32_t sample_rate) override;

private:
	struct Data;

	template <typename Callback>
	std::invoke_result_t<Callback> sync(Callback &&callback);

	template <typename Callback>
	void enumerateDevices(Callback &&callback) const;

	void takeData();
	void openDevice();
	void stopCaptureOnThread();
	void closeDevice();
	void startCaptureOnThread();

	bool validateDeviceId();
	[[nodiscard]] std::string computeDefaultDeviceId() const;
	void restartQueued();

	//void handleEvent(
	//	ALenum eventType,
	//	ALuint object,
	//	ALuint param,
	//	ALsizei length,
	//	const ALchar *message);

	rtc::Thread *_thread = nullptr;
	std::unique_ptr<Data> _data;

	ALCdevice *_device = nullptr;
	//ALCcontext *_context = nullptr;
	std::string _deviceId;
	int _requestedRate = 0;
	int _rate = 0;
	bool _recording = false;
	bool _failed = false;

	webrtc::AudioDeviceBuffer *_audioDeviceBuffer = nullptr;

};

} // namespace Webrtc::details
