// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_audio_input_tester.h"

#include "webrtc/webrtc_device_common.h"
#include "webrtc/webrtc_create_adm.h"
#include "crl/crl_object_on_thread.h"
#include "crl/crl_async.h"

#include <media/engine/webrtc_media_engine.h>
#include <api/task_queue/default_task_queue_factory.h>

namespace Webrtc {

class AudioInputTester::Impl : public webrtc::AudioTransport {
public:
	explicit Impl(const std::shared_ptr<std::atomic<int>> &maxSample);
	~Impl();

	void setDeviceId(const DeviceResolvedId &deviceId);

private:
	void init();
	void restart();

	int32_t RecordedDataIsAvailable(
		const void* audioSamples,
		const size_t nSamples,
		const size_t nBytesPerSample,
		const size_t nChannels,
		const uint32_t samplesPerSec,
		const uint32_t totalDelayMS,
		const int32_t clockDrift,
		const uint32_t currentMicLevel,
		const bool keyPressed,
		uint32_t& newMicLevel) override;

	// Implementation has to setup safe values for all specified out parameters.
	int32_t NeedMorePlayData(
		const size_t nSamples,
		const size_t nBytesPerSample,
		const size_t nChannels,
		const uint32_t samplesPerSec,
		void* audioSamples,
		size_t& nSamplesOut,  // NOLINT
		int64_t* elapsed_time_ms,
		int64_t* ntp_time_ms) override;

	// Method to pull mixed render audio data from all active VoE channels.
	// The data will not be passed as reference for audio processing internally.
	void PullRenderData(
		int bits_per_sample,
		int sample_rate,
		size_t number_of_channels,
		size_t number_of_frames,
		void* audio_data,
		int64_t* elapsed_time_ms,
		int64_t* ntp_time_ms) override;

	std::shared_ptr<std::atomic<int>> _maxSample;
	std::unique_ptr<webrtc::TaskQueueFactory> _taskQueueFactory;
	rtc::scoped_refptr<webrtc::AudioDeviceModule> _adm;
	Fn<void(DeviceResolvedId)> _setDeviceIdCallback;
	DeviceResolvedId _deviceId;

};

AudioInputTester::Impl::Impl(
	const std::shared_ptr<std::atomic<int>> &maxSample)
: _maxSample(std::move(maxSample))
, _taskQueueFactory(webrtc::CreateDefaultTaskQueueFactory())
, _deviceId{ .type = DeviceType::Capture } {
	const auto saveSetDeviceIdCallback = [=](
			Fn<void(DeviceResolvedId)> setDeviceIdCallback) {
		_setDeviceIdCallback = std::move(setDeviceIdCallback);
		if (!_deviceId.isDefault()) {
			_setDeviceIdCallback(_deviceId);
			restart();
		}
	};
	_adm = CreateAudioDeviceModule(
		_taskQueueFactory.get(),
		saveSetDeviceIdCallback);
	init();
}

AudioInputTester::Impl::~Impl() {
	if (_adm) {
		_adm->StopRecording();
		_adm->RegisterAudioCallback(nullptr);
		_adm->Terminate();
	}
}

void AudioInputTester::Impl::init() {
	if (!_adm) {
		return;
	}
	_adm->Init();
	_adm->RegisterAudioCallback(this);
}

void AudioInputTester::Impl::setDeviceId(const DeviceResolvedId &deviceId) {
	_deviceId = deviceId;
	if (_setDeviceIdCallback) {
		_setDeviceIdCallback(_deviceId);
		restart();
	}
}

void AudioInputTester::Impl::restart() {
	if (!_adm) {
		return;
	}
	_adm->StopRecording();
	_adm->SetRecordingDevice(0);
	if (_adm->InitRecording() == 0) {
		_adm->StartRecording();
	}
}

int32_t AudioInputTester::Impl::RecordedDataIsAvailable(
		const void* audioSamples,
		const size_t nSamples,
		const size_t nBytesPerSample,
		const size_t nChannels,
		const uint32_t samplesPerSec,
		const uint32_t totalDelayMS,
		const int32_t clockDrift,
		const uint32_t currentMicLevel,
		const bool keyPressed,
		uint32_t& newMicLevel) {
	const auto channels = nBytesPerSample / sizeof(int16_t);
	if (channels > 0 && !(nBytesPerSample % sizeof(int16_t))) {
		auto max = 0;
		auto values = static_cast<const int16_t*>(audioSamples);
		for (auto i = size_t(); i != nSamples * channels; ++i) {
			if (max < values[i]) {
				max = values[i];
			}
		}
		const auto now = _maxSample->load();
		if (max > now) {
			_maxSample->store(max);
		}
	}
	newMicLevel = currentMicLevel;
	return 0;
}

int32_t AudioInputTester::Impl::NeedMorePlayData(const size_t nSamples,
		const size_t nBytesPerSample,
		const size_t nChannels,
		const uint32_t samplesPerSec,
		void* audioSamples,
		size_t& nSamplesOut,
		int64_t* elapsed_time_ms,
		int64_t* ntp_time_ms) {
	nSamplesOut = 0;
	return 0;
}

void AudioInputTester::Impl::PullRenderData(int bits_per_sample,
		int sample_rate,
		size_t number_of_channels,
		size_t number_of_frames,
		void* audio_data,
		int64_t* elapsed_time_ms,
		int64_t* ntp_time_ms) {
}

AudioInputTester::AudioInputTester(rpl::producer<DeviceResolvedId> deviceId)
: _maxSample(std::make_shared<std::atomic<int>>(0))
, _impl(std::as_const(_maxSample)) {
	std::move(
		deviceId
	) | rpl::start_with_next([=](const DeviceResolvedId &id) {
		_impl.with([=](Impl &impl) {
			impl.setDeviceId(id);
		});
	}, _lifetime);
}

AudioInputTester::~AudioInputTester() = default;

float AudioInputTester::getAndResetLevel() {
	return _maxSample->exchange(0) / float(INT16_MAX);
}

} // namespace Webrtc
