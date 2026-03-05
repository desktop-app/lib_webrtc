// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_create_adm.h"

#include "webrtc/details/webrtc_openal_adm.h"

#include <api/make_ref_counted.h>
#include <modules/audio_device/include/audio_device.h>
#include <modules/audio_device/include/audio_device_factory.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#ifdef WEBRTC_WIN
#include "webrtc/platform/win/webrtc_loopback_adm_win.h"
#elif defined WEBRTC_LINUX // WEBRTC_WIN
#include "webrtc/platform/linux/webrtc_loopback_adm_linux.h"
#include <al.h>
#include <alc.h>
#endif // WEBRTC_WIN || WEBRTC_LINUX

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
#elif defined WEBRTC_LINUX // WEBRTC_WIN
	auto result = rtc::make_ref_counted<details::AudioDeviceLoopbackLinux>(
		factory);
	if (result->Init() == 0) {
		return result;
	}
#endif // WEBRTC_WIN || WEBRTC_LINUX
	return nullptr;
}

auto LoopbackAudioDeviceModuleCreator()
-> std::function<AudioDeviceModulePtr(webrtc::TaskQueueFactory*)> {
	return CreateLoopbackAudioDeviceModule;
}

bool LoopbackAudioCaptureSupported() {
#ifdef WEBRTC_WIN
	return true;
#elif defined WEBRTC_LINUX // WEBRTC_WIN
	return details::AudioDeviceLoopbackLinux::IsSupported();
#else // WEBRTC_WIN || WEBRTC_LINUX
	return false;
#endif // WEBRTC_WIN || WEBRTC_LINUX
}

namespace {

// ---------------------------------------------------------------------------
// LoopbackCollector — thread-safe mono sample ring buffer.
// Loopback capture threads push stereo/mono frames in; MixingAudioTransport
// reads them out and mixes them into the outgoing microphone stream.
// ---------------------------------------------------------------------------

constexpr size_t kMaxBufferedFrames = 48000 * 2; // 2 seconds at 48 kHz
constexpr int kCaptureFrequency = 48000;
constexpr int kCapturePollMs = 10;
constexpr int kCaptureFramesPerPoll =
	kCaptureFrequency * kCapturePollMs / 1000; // 480

class LoopbackCollector {
public:
	void pushSamples(
			const int16_t *src,
			size_t frames,
			size_t srcChannels) {
		auto lock = std::lock_guard(_mutex);
		if (_buffer.size() + frames > kMaxBufferedFrames) {
			const auto overflow =
				(_buffer.size() + frames) - kMaxBufferedFrames;
			_buffer.erase(_buffer.begin(), _buffer.begin() + overflow);
		}
		if (srcChannels >= 2) {
			for (size_t i = 0; i < frames; ++i) {
				const auto mixed =
					(static_cast<int32_t>(src[i * srcChannels])
					+ static_cast<int32_t>(src[i * srcChannels + 1])) / 2;
				_buffer.push_back(static_cast<int16_t>(mixed));
			}
		} else {
			_buffer.insert(_buffer.end(), src, src + frames);
		}
	}

	void readAndMix(
			int16_t *destination,
			size_t frames,
			size_t channels) {
		auto lock = std::lock_guard(_mutex);
		const auto available = std::min(frames, _buffer.size());
		if (available == 0) {
			return;
		}
		for (size_t i = 0; i < available; ++i) {
			const auto loopbackSample =
				static_cast<int32_t>(_buffer[i]);
			for (size_t ch = 0; ch < channels; ++ch) {
				const auto idx = i * channels + ch;
				auto mixed =
					static_cast<int32_t>(destination[idx]) + loopbackSample;
				mixed = std::max(mixed, int32_t(-32768));
				mixed = std::min(mixed, int32_t(32767));
				destination[idx] = static_cast<int16_t>(mixed);
			}
		}
		_buffer.erase(_buffer.begin(), _buffer.begin() + available);
	}

private:
	std::mutex _mutex;
	std::vector<int16_t> _buffer;
};

// ---------------------------------------------------------------------------
// DirectLoopbackCapture — Linux-only background thread that reads from the
// PulseAudio monitor source via OpenAL Soft and feeds LoopbackCollector.
// ---------------------------------------------------------------------------

#ifdef WEBRTC_LINUX

class DirectLoopbackCapture {
public:
	explicit DirectLoopbackCapture(
		std::shared_ptr<LoopbackCollector> collector)
	: _collector(std::move(collector)) {
	}

	~DirectLoopbackCapture() {
		stop();
	}

	bool start() {
		if (_running.load()) {
			return true;
		}
		const auto deviceId = findMonitorDevice();
		if (deviceId.empty()) {
			return false;
		}
		_shouldStop.store(false);
		_running.store(true);
		_thread = std::thread([this, deviceId] {
			captureLoop(deviceId);
			_running.store(false);
		});
		return true;
	}

	void stop() {
		if (!_running.load()) {
			return;
		}
		_shouldStop.store(true);
		if (_thread.joinable()) {
			_thread.join();
		}
	}

private:
	static std::string findMonitorDevice() {
		const auto *devices = alcGetString(
			nullptr,
			ALC_CAPTURE_DEVICE_SPECIFIER);
		if (!devices) {
			return {};
		}

		auto candidates = std::vector<std::string>();
		for (auto i = devices; *i != '\0';) {
			auto id = std::string(i);
			auto lower = id;
			for (auto &ch : lower) {
				ch = char(std::tolower(
					static_cast<unsigned char>(ch)));
			}
			if (lower.find("monitor") != std::string::npos
				|| lower.find("loopback") != std::string::npos
				|| lower.find("stereo mix") != std::string::npos
				|| lower.find("what u hear") != std::string::npos) {
				candidates.push_back(std::move(id));
			}
			i += id.size() + 1;
		}
		if (candidates.empty()) {
			return {};
		}

		const auto *defaultPlayback = alcGetString(
			nullptr,
			ALC_DEFAULT_ALL_DEVICES_SPECIFIER);
		if (defaultPlayback) {
			auto sinkName = std::string(defaultPlayback);
			constexpr auto kPrefix = "OpenAL Soft on ";
			constexpr auto kPrefixLen = sizeof("OpenAL Soft on ") - 1;
			if (sinkName.rfind(kPrefix, 0) == 0) {
				sinkName = sinkName.substr(kPrefixLen);
			}
			auto sinkLower = sinkName;
			for (auto &ch : sinkLower) {
				ch = char(std::tolower(
					static_cast<unsigned char>(ch)));
			}
			for (const auto &id : candidates) {
				auto lower = id;
				for (auto &ch : lower) {
					ch = char(std::tolower(
						static_cast<unsigned char>(ch)));
				}
				if (lower.find(sinkLower) != std::string::npos) {
					return id;
				}
			}
		}
		return candidates.front();
	}

	void captureLoop(const std::string &deviceId) {
		// Try stereo first, fall back to mono.
		auto *device = alcCaptureOpenDevice(
			deviceId.c_str(),
			kCaptureFrequency,
			AL_FORMAT_STEREO16,
			kCaptureFrequency / 4);
		auto channels = 2;
		if (!device) {
			device = alcCaptureOpenDevice(
				deviceId.c_str(),
				kCaptureFrequency,
				AL_FORMAT_MONO16,
				kCaptureFrequency / 4);
			channels = 1;
		}
		if (!device) {
			return;
		}

		alcCaptureStart(device);
		auto readBuffer = std::vector<int16_t>(
			kCaptureFramesPerPoll * channels);

		while (!_shouldStop.load(std::memory_order_acquire)) {
			std::this_thread::sleep_for(
				std::chrono::milliseconds(kCapturePollMs));

			auto samples = ALint(0);
			alcGetIntegerv(
				device,
				ALC_CAPTURE_SAMPLES,
				1,
				&samples);
			while (samples >= ALint(kCaptureFramesPerPoll)) {
				alcCaptureSamples(
					device,
					reinterpret_cast<ALCvoid *>(readBuffer.data()),
					kCaptureFramesPerPoll);
				if (_collector) {
					_collector->pushSamples(
						readBuffer.data(),
						kCaptureFramesPerPoll,
						channels);
				}
				samples -= kCaptureFramesPerPoll;
			}
		}

		alcCaptureStop(device);
		alcCaptureCloseDevice(device);
	}

	std::shared_ptr<LoopbackCollector> _collector;
	std::thread _thread;
	std::atomic<bool> _running = false;
	std::atomic<bool> _shouldStop = false;
};

#endif // WEBRTC_LINUX

// ---------------------------------------------------------------------------
// MixingAudioTransport — wraps the real WebRTC AudioTransport and mixes
// loopback audio into every RecordedDataIsAvailable() call.
// ---------------------------------------------------------------------------

class MixingAudioTransport final : public webrtc::AudioTransport {
public:
	MixingAudioTransport(
		webrtc::AudioTransport *inner,
		std::shared_ptr<LoopbackCollector> collector)
	: _inner(inner)
	, _collector(std::move(collector)) {
	}

	int32_t RecordedDataIsAvailable(
			const void *audioSamples,
			size_t nSamples,
			size_t nBytesPerSample,
			size_t nChannels,
			uint32_t samplesPerSec,
			uint32_t totalDelayMS,
			int32_t clockDrift,
			uint32_t currentMicLevel,
			bool keyPressed,
			uint32_t &newMicLevel) override {
		const auto mixing = _mixingEnabled.load(std::memory_order_relaxed);
		const auto muted = _microphoneMuted.load(std::memory_order_relaxed);

		// Fast path: nothing to do, hand off directly.
		if (!mixing && !muted) {
			return _inner->RecordedDataIsAvailable(
				audioSamples,
				nSamples,
				nBytesPerSample,
				nChannels,
				samplesPerSec,
				totalDelayMS,
				clockDrift,
				currentMicLevel,
				keyPressed,
				newMicLevel);
		}

		if (nBytesPerSample != 2) {
			return _inner->RecordedDataIsAvailable(
				audioSamples,
				nSamples,
				nBytesPerSample,
				nChannels,
				samplesPerSec,
				totalDelayMS,
				clockDrift,
				currentMicLevel,
				keyPressed,
				newMicLevel);
		}

		const auto totalSamples = nSamples * nChannels;
		_mixBuffer.resize(totalSamples);
		if (muted) {
			// Zero out mic samples so the microphone is silent while
			// still allowing system audio (loopback) to flow through.
			std::memset(
				_mixBuffer.data(),
				0,
				totalSamples * sizeof(int16_t));
		} else {
			std::memcpy(
				_mixBuffer.data(),
				audioSamples,
				totalSamples * sizeof(int16_t));
		}
		if (mixing && _collector) {
			_collector->readAndMix(_mixBuffer.data(), nSamples, nChannels);
		}

		return _inner->RecordedDataIsAvailable(
			_mixBuffer.data(),
			nSamples,
			nBytesPerSample,
			nChannels,
			samplesPerSec,
			totalDelayMS,
			clockDrift,
			currentMicLevel,
			keyPressed,
			newMicLevel);
	}

	int32_t NeedMorePlayData(
			size_t nSamples,
			size_t nBytesPerSample,
			size_t nChannels,
			uint32_t samplesPerSec,
			void *audioSamples,
			size_t &nSamplesOut,
			int64_t *elapsed_time_ms,
			int64_t *ntp_time_ms) override {
		const auto result = _inner->NeedMorePlayData(
			nSamples,
			nBytesPerSample,
			nChannels,
			samplesPerSec,
			audioSamples,
			nSamplesOut,
			elapsed_time_ms,
			ntp_time_ms);
		// Scale playback samples by the requested volume (1.0 = unity gain).
		const auto volume =
			_playbackVolume.load(std::memory_order_relaxed);
		if (result == 0
			&& volume < 0.999f
			&& nBytesPerSample == 2
			&& nSamplesOut > 0) {
			const auto total = nSamplesOut * nChannels;
			auto *samples = static_cast<int16_t *>(audioSamples);
			for (size_t i = 0; i < total; ++i) {
				samples[i] = static_cast<int16_t>(
					samples[i] * volume);
			}
		}
		return result;
	}

	void PullRenderData(
			int bits_per_sample,
			int sample_rate,
			size_t number_of_channels,
			size_t number_of_frames,
			void *audio_data,
			int64_t *elapsed_time_ms,
			int64_t *ntp_time_ms) override {
		_inner->PullRenderData(
			bits_per_sample,
			sample_rate,
			number_of_channels,
			number_of_frames,
			audio_data,
			elapsed_time_ms,
			ntp_time_ms);
	}

	void setMixingEnabled(bool enabled) {
		_mixingEnabled.store(enabled, std::memory_order_relaxed);
	}

	void setMicrophoneMuted(bool muted) {
		_microphoneMuted.store(muted, std::memory_order_relaxed);
	}

	void setPlaybackVolume(float volume) {
		_playbackVolume.store(volume, std::memory_order_relaxed);
	}

private:
	webrtc::AudioTransport *_inner = nullptr;
	std::shared_ptr<LoopbackCollector> _collector;
	std::atomic<bool> _mixingEnabled = false;
	std::atomic<bool> _microphoneMuted = false;
	std::atomic<float> _playbackVolume = 1.f;
	std::vector<int16_t> _mixBuffer;
};

} // namespace

// ---------------------------------------------------------------------------
// MixingAudioDeviceModule — ADM wrapper that injects loopback audio into the
// microphone stream via MixingAudioTransport.
// ---------------------------------------------------------------------------

namespace details {

class MixingAudioDeviceModule : public webrtc::AudioDeviceModule {
public:
	MixingAudioDeviceModule(
		rtc::scoped_refptr<webrtc::AudioDeviceModule> inner,
		webrtc::TaskQueueFactory *taskQueueFactory,
		std::shared_ptr<MixingAudioControl> control)
	: _inner(std::move(inner))
	, _taskQueueFactory(taskQueueFactory)
	, _control(std::move(control))
	, _collector(std::make_shared<LoopbackCollector>()) {
		if (_control) {
			_control->attach(this);
		}
	}

	~MixingAudioDeviceModule() override {
		stopLoopback();
		if (_control) {
			_control->detach();
		}
	}

	void setLoopbackEnabled(bool enabled) {
		if (enabled == _loopbackActive) {
			return;
		}
		if (enabled) {
			startLoopback();
		} else {
			stopLoopback();
		}
		if (_mixingTransport) {
			_mixingTransport->setMixingEnabled(_loopbackActive);
		}
	}

	void setMicrophoneMuted(bool muted) {
		if (_mixingTransport) {
			_mixingTransport->setMicrophoneMuted(muted);
		}
	}

	void setPlaybackVolume(float volume) {
		if (_mixingTransport) {
			_mixingTransport->setPlaybackVolume(volume);
		}
	}

	// webrtc::AudioDeviceModule interface — delegate everything to _inner.

	int32_t ActiveAudioLayer(AudioLayer *audioLayer) const override {
		return _inner->ActiveAudioLayer(audioLayer);
	}

	int32_t RegisterAudioCallback(
			webrtc::AudioTransport *audioCallback) override {
		if (audioCallback) {
			_mixingTransport = std::make_unique<MixingAudioTransport>(
				audioCallback,
				_collector);
			_mixingTransport->setMixingEnabled(_loopbackActive);
			if (_control) {
				auto lock = std::lock_guard(_control->_mutex);
				if (_control->_microphoneMuted) {
					_mixingTransport->setMicrophoneMuted(true);
				}
				if (_control->_playbackVolume < 0.999f) {
					_mixingTransport->setPlaybackVolume(
						_control->_playbackVolume);
				}
			}
			return _inner->RegisterAudioCallback(_mixingTransport.get());
		}
		_mixingTransport = nullptr;
		return _inner->RegisterAudioCallback(nullptr);
	}

	int32_t Init() override {
		return _inner->Init();
	}

	int32_t Terminate() override {
		stopLoopback();
		return _inner->Terminate();
	}

	bool Initialized() const override {
		return _inner->Initialized();
	}

	int16_t PlayoutDevices() override {
		return _inner->PlayoutDevices();
	}

	int16_t RecordingDevices() override {
		return _inner->RecordingDevices();
	}

	int32_t PlayoutDeviceName(
			uint16_t index,
			char name[webrtc::kAdmMaxDeviceNameSize],
			char guid[webrtc::kAdmMaxGuidSize]) override {
		return _inner->PlayoutDeviceName(index, name, guid);
	}

	int32_t RecordingDeviceName(
			uint16_t index,
			char name[webrtc::kAdmMaxDeviceNameSize],
			char guid[webrtc::kAdmMaxGuidSize]) override {
		return _inner->RecordingDeviceName(index, name, guid);
	}

	int32_t SetPlayoutDevice(uint16_t index) override {
		return _inner->SetPlayoutDevice(index);
	}

	int32_t SetPlayoutDevice(WindowsDeviceType device) override {
		return _inner->SetPlayoutDevice(device);
	}

	int32_t SetRecordingDevice(uint16_t index) override {
		return _inner->SetRecordingDevice(index);
	}

	int32_t SetRecordingDevice(WindowsDeviceType device) override {
		return _inner->SetRecordingDevice(device);
	}

	int32_t PlayoutIsAvailable(bool *available) override {
		return _inner->PlayoutIsAvailable(available);
	}

	int32_t InitPlayout() override {
		return _inner->InitPlayout();
	}

	bool PlayoutIsInitialized() const override {
		return _inner->PlayoutIsInitialized();
	}

	int32_t RecordingIsAvailable(bool *available) override {
		return _inner->RecordingIsAvailable(available);
	}

	int32_t InitRecording() override {
		return _inner->InitRecording();
	}

	bool RecordingIsInitialized() const override {
		return _inner->RecordingIsInitialized();
	}

	int32_t StartPlayout() override {
		return _inner->StartPlayout();
	}

	int32_t StopPlayout() override {
		return _inner->StopPlayout();
	}

	bool Playing() const override {
		return _inner->Playing();
	}

	int32_t StartRecording() override {
		return _inner->StartRecording();
	}

	int32_t StopRecording() override {
		return _inner->StopRecording();
	}

	bool Recording() const override {
		return _inner->Recording();
	}

	int32_t InitSpeaker() override {
		return _inner->InitSpeaker();
	}

	bool SpeakerIsInitialized() const override {
		return _inner->SpeakerIsInitialized();
	}

	int32_t InitMicrophone() override {
		return _inner->InitMicrophone();
	}

	bool MicrophoneIsInitialized() const override {
		return _inner->MicrophoneIsInitialized();
	}

	int32_t SpeakerVolumeIsAvailable(bool *available) override {
		return _inner->SpeakerVolumeIsAvailable(available);
	}

	int32_t SetSpeakerVolume(uint32_t volume) override {
		return _inner->SetSpeakerVolume(volume);
	}

	int32_t SpeakerVolume(uint32_t *volume) const override {
		return _inner->SpeakerVolume(volume);
	}

	int32_t MaxSpeakerVolume(uint32_t *maxVolume) const override {
		return _inner->MaxSpeakerVolume(maxVolume);
	}

	int32_t MinSpeakerVolume(uint32_t *minVolume) const override {
		return _inner->MinSpeakerVolume(minVolume);
	}

	int32_t MicrophoneVolumeIsAvailable(bool *available) override {
		return _inner->MicrophoneVolumeIsAvailable(available);
	}

	int32_t SetMicrophoneVolume(uint32_t volume) override {
		return _inner->SetMicrophoneVolume(volume);
	}

	int32_t MicrophoneVolume(uint32_t *volume) const override {
		return _inner->MicrophoneVolume(volume);
	}

	int32_t MaxMicrophoneVolume(uint32_t *maxVolume) const override {
		return _inner->MaxMicrophoneVolume(maxVolume);
	}

	int32_t MinMicrophoneVolume(uint32_t *minVolume) const override {
		return _inner->MinMicrophoneVolume(minVolume);
	}

	int32_t MicrophoneMuteIsAvailable(bool *available) override {
		return _inner->MicrophoneMuteIsAvailable(available);
	}

	int32_t SetMicrophoneMute(bool enable) override {
		return _inner->SetMicrophoneMute(enable);
	}

	int32_t MicrophoneMute(bool *enabled) const override {
		return _inner->MicrophoneMute(enabled);
	}

	int32_t SpeakerMuteIsAvailable(bool *available) override {
		return _inner->SpeakerMuteIsAvailable(available);
	}

	int32_t SetSpeakerMute(bool enable) override {
		return _inner->SetSpeakerMute(enable);
	}

	int32_t SpeakerMute(bool *enabled) const override {
		return _inner->SpeakerMute(enabled);
	}

	int32_t StereoPlayoutIsAvailable(bool *available) const override {
		return _inner->StereoPlayoutIsAvailable(available);
	}

	int32_t SetStereoPlayout(bool enable) override {
		return _inner->SetStereoPlayout(enable);
	}

	int32_t StereoPlayout(bool *enabled) const override {
		return _inner->StereoPlayout(enabled);
	}

	int32_t StereoRecordingIsAvailable(bool *available) const override {
		return _inner->StereoRecordingIsAvailable(available);
	}

	int32_t SetStereoRecording(bool enable) override {
		return _inner->SetStereoRecording(enable);
	}

	int32_t StereoRecording(bool *enabled) const override {
		return _inner->StereoRecording(enabled);
	}

	int32_t PlayoutDelay(uint16_t *delayMS) const override {
		return _inner->PlayoutDelay(delayMS);
	}

	bool BuiltInAECIsAvailable() const override {
		return _inner->BuiltInAECIsAvailable();
	}

	bool BuiltInAGCIsAvailable() const override {
		return _inner->BuiltInAGCIsAvailable();
	}

	bool BuiltInNSIsAvailable() const override {
		return _inner->BuiltInNSIsAvailable();
	}

	int32_t EnableBuiltInAEC(bool enable) override {
		return _inner->EnableBuiltInAEC(enable);
	}

	int32_t EnableBuiltInAGC(bool enable) override {
		return _inner->EnableBuiltInAGC(enable);
	}

	int32_t EnableBuiltInNS(bool enable) override {
		return _inner->EnableBuiltInNS(enable);
	}

private:
	void startLoopback() {
		if (_loopbackActive) {
			return;
		}
#ifdef WEBRTC_LINUX
		_loopbackCapture = std::make_unique<DirectLoopbackCapture>(
			_collector);
		if (!_loopbackCapture->start()) {
			_loopbackCapture = nullptr;
			return;
		}
		_loopbackActive = true;
#elif defined WEBRTC_WIN
		_loopbackAdm = CreateLoopbackAudioDeviceModule(_taskQueueFactory);
		if (!_loopbackAdm) {
			return;
		}
		_loopbackTransport = std::make_unique<LoopbackAdmTransport>(
			_collector);
		_loopbackAdm->RegisterAudioCallback(_loopbackTransport.get());
		_loopbackAdm->InitMicrophone();
		_loopbackAdm->InitRecording();
		_loopbackAdm->StartRecording();
		_loopbackActive = true;
#endif
	}

	void stopLoopback() {
		if (!_loopbackActive) {
			return;
		}
#ifdef WEBRTC_LINUX
		_loopbackCapture = nullptr;
#elif defined WEBRTC_WIN
		if (_loopbackAdm) {
			_loopbackAdm->StopRecording();
			_loopbackAdm->RegisterAudioCallback(nullptr);
			_loopbackAdm->Terminate();
			_loopbackAdm = nullptr;
		}
		_loopbackTransport = nullptr;
#endif
		_loopbackActive = false;
	}

#ifdef WEBRTC_WIN
	class LoopbackAdmTransport final : public webrtc::AudioTransport {
	public:
		explicit LoopbackAdmTransport(
			std::shared_ptr<LoopbackCollector> collector)
		: _collector(std::move(collector)) {
		}

		int32_t RecordedDataIsAvailable(
				const void *audioSamples,
				size_t nSamples,
				size_t nBytesPerSample,
				size_t nChannels,
				uint32_t /*samplesPerSec*/,
				uint32_t /*totalDelayMS*/,
				int32_t /*clockDrift*/,
				uint32_t currentMicLevel,
				bool /*keyPressed*/,
				uint32_t &newMicLevel) override {
			newMicLevel = currentMicLevel;
			if (!audioSamples || !nSamples || nBytesPerSample != 2) {
				return 0;
			}
			_collector->pushSamples(
				static_cast<const int16_t *>(audioSamples),
				nSamples,
				nChannels);
			return 0;
		}

		int32_t NeedMorePlayData(
				size_t, size_t, size_t, uint32_t,
				void *, size_t &nSamplesOut,
				int64_t *, int64_t *) override {
			nSamplesOut = 0;
			return 0;
		}

		void PullRenderData(
				int, int, size_t, size_t,
				void *, int64_t *, int64_t *) override {
		}

	private:
		std::shared_ptr<LoopbackCollector> _collector;
	};
#endif // WEBRTC_WIN

	rtc::scoped_refptr<webrtc::AudioDeviceModule> _inner;
	webrtc::TaskQueueFactory *_taskQueueFactory = nullptr;
	std::shared_ptr<MixingAudioControl> _control;

	std::shared_ptr<LoopbackCollector> _collector;
	std::unique_ptr<MixingAudioTransport> _mixingTransport;

#ifdef WEBRTC_LINUX
	std::unique_ptr<DirectLoopbackCapture> _loopbackCapture;
#elif defined WEBRTC_WIN
	rtc::scoped_refptr<webrtc::AudioDeviceModule> _loopbackAdm;
	std::unique_ptr<LoopbackAdmTransport> _loopbackTransport;
#endif
	bool _loopbackActive = false;
};

} // namespace details

void MixingAudioControl::setLoopbackEnabled(bool enabled) {
	auto lock = std::lock_guard(_mutex);
	_pendingEnabled = enabled;
	if (_module) {
		_module->setLoopbackEnabled(enabled);
	}
}

bool MixingAudioControl::loopbackEnabled() const {
	return _pendingEnabled;
}

void MixingAudioControl::setMicrophoneMuted(bool muted) {
	auto lock = std::lock_guard(_mutex);
	_microphoneMuted = muted;
	if (_module) {
		_module->setMicrophoneMuted(muted);
	}
}

bool MixingAudioControl::microphoneMuted() const {
	return _microphoneMuted;
}

void MixingAudioControl::setPlaybackVolume(float volume) {
	auto lock = std::lock_guard(_mutex);
	_playbackVolume = volume;
	if (_module) {
		_module->setPlaybackVolume(volume);
	}
}

float MixingAudioControl::playbackVolume() const {
	return _playbackVolume;
}

void MixingAudioControl::attach(details::MixingAudioDeviceModule *module) {
	auto lock = std::lock_guard(_mutex);
	_module = module;
	if (_module) {
		if (_pendingEnabled) {
			_module->setLoopbackEnabled(true);
		}
		if (_microphoneMuted) {
			_module->setMicrophoneMuted(true);
		}
		if (_playbackVolume < 0.999f) {
			_module->setPlaybackVolume(_playbackVolume);
		}
	}
}

void MixingAudioControl::detach() {
	auto lock = std::lock_guard(_mutex);
	_module = nullptr;
}

auto MixingAudioDeviceModuleCreator(
		std::function<AudioDeviceModulePtr(webrtc::TaskQueueFactory*)>
			innerCreator,
		std::shared_ptr<MixingAudioControl> control)
-> std::function<AudioDeviceModulePtr(webrtc::TaskQueueFactory*)> {
	return [innerCreator = std::move(innerCreator),
			control = std::move(control)](
			webrtc::TaskQueueFactory *factory)
		-> rtc::scoped_refptr<webrtc::AudioDeviceModule> {
		auto inner = innerCreator(factory);
		if (!inner) {
			return nullptr;
		}
		return rtc::make_ref_counted<details::MixingAudioDeviceModule>(
			std::move(inner),
			factory,
			std::move(control));
	};
}

} // namespace Webrtc
