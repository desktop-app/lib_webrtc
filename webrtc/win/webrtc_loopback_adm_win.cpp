// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/win/webrtc_loopback_adm_win.h"

#include "rtc_base/logging.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "modules/audio_processing/include/audio_frame_proxies.h"
#include "api/audio/audio_frame.h"

namespace Webrtc::details {
namespace {

constexpr auto kWantedFrequency = 48000;
constexpr auto kWantedChannels = 2;
constexpr auto kBufferSizeMs = crl::time(10);
constexpr auto kWantedBitsPerSample = 16;
constexpr auto kProcessInterval = crl::time(10);

constexpr auto kFarEndFrequency = 48000;
constexpr auto kFarEndChannels = 2;
constexpr auto kFarEndFramesCount = 1000 / kBufferSizeMs;
constexpr auto kFarEndChannelFrameSize = (kFarEndFrequency * kBufferSizeMs)
	/ 1000;
static_assert(kFarEndChannelFrameSize * 1000
	== kFarEndFrequency * kBufferSizeMs);

constexpr auto kMaxEchoDelay = crl::time(1000);

enum class FarEndFrameState : uint8_t {
	Empty,
	Writing,
	Reading,
	Ready,
};

struct FarEndFrame {
	std::array<std::int16_t, kFarEndChannelFrameSize * kFarEndChannels> data;
	crl::time when;
	std::atomic<FarEndFrameState> state;
};

struct FarEnd {
	std::array<FarEndFrame, kFarEndFramesCount> frames;
	std::atomic<int> WriteIndex;
	std::atomic<int> ReadIndex;
};

std::atomic<bool> LoopbackCaptureActive/* = false*/;
FarEnd LoopbackFarEnd/* = { .. 0 .. }*/;

[[nodiscard]] int PartSize(int frequency) {
	return (frequency * kBufferSizeMs + 999) / 1000;
}

void SetStringToArray(const std::string &string, char *array, int size) {
	const auto length = std::min(int(string.size()), size - 1);
	if (length > 0) {
		memcpy(array, string.data(), length);
	}
	array[length] = 0;
}

[[nodiscard]] auto CreateAudioProcessing()
-> std::unique_ptr<webrtc::AudioProcessing> {
	auto result = std::unique_ptr<webrtc::AudioProcessing>(
		webrtc::AudioProcessingBuilder().Create(webrtc::Config()));

	auto config = webrtc::AudioProcessing::Config();
	config.echo_canceller.enabled = true;
	config.echo_canceller.mobile_mode = true;
	result->ApplyConfig(config);
	return result;
}

} // namespace

bool IsLoopbackCaptureActive() {
	return LoopbackCaptureActive;
}

void LoopbackCapturePushFarEnd(
		crl::time when,
		const QByteArray &samples,
		int frequency,
		int channels) {
	Expects(frequency == kFarEndFrequency);
	Expects(channels == kFarEndChannels);
	Expects(samples.size()
		== kFarEndChannelFrameSize * kFarEndChannels * sizeof(std::int16_t));

	using State = FarEndFrameState;

	const auto index = LoopbackFarEnd.WriteIndex.load(
		std::memory_order_relaxed);
	auto &frame = LoopbackFarEnd.frames[index];
	auto empty = State::Empty;
	if (!frame.state.compare_exchange_strong(empty, State::Writing)) {
		// No space.
		return;
	}
	memcpy(frame.data.data(), samples.constData(), samples.size());

	RTC_LOG(LS_ERROR) << "Far End frame written at " << index
		<< ", when: " << when;

	frame.when = when;
	frame.state.store(State::Ready, std::memory_order_release);
	LoopbackFarEnd.WriteIndex.store(
		(index + 1) % kFarEndFramesCount,
		std::memory_order_relaxed);
}

std::optional<crl::time> LoopbackCaptureTakeFarEnd(
		webrtc::AudioFrame &to,
		crl::time nearEndWhen) {
	Expects(to.sample_rate_hz_ == kFarEndFrequency);
	Expects(to.num_channels_ == kFarEndChannels);
	Expects(to.samples_per_channel_ == kFarEndChannelFrameSize);

	using State = FarEndFrameState;

	while (true) {
		const auto index = LoopbackFarEnd.ReadIndex.load(
			std::memory_order_relaxed);
		auto &frame = LoopbackFarEnd.frames[index];
		auto ready = State::Ready;
		if (!frame.state.compare_exchange_strong(ready, State::Reading)) {
			// Not ready.
			//RTC_LOG(LS_ERROR) << "Near End when: " << nearEndWhen
			//	<< "; ERROR Far End frame not ready at " << index;
			return std::nullopt;
		}
		const auto delay = frame.when - nearEndWhen;
		if (delay > kMaxEchoDelay) {
			// Too far away, wait.
			frame.state.store(State::Ready, std::memory_order_relaxed);
			//RTC_LOG(LS_ERROR) << "Near End when: " << nearEndWhen
			//	<< "; ERROR Far End frame too far away at " << index
			//	<< ", when: " << frame.when << ", delay: " << delay;
			return std::nullopt;
		}
		if (delay >= 0) {
			memcpy(to.mutable_data(), frame.data.data(), frame.data.size());
		}
		frame.state.store(State::Empty, std::memory_order_release);
		LoopbackFarEnd.ReadIndex.store(
			(index + 1) % kFarEndFramesCount,
			std::memory_order_relaxed);
		if (delay >= 0) {
			//RTC_LOG(LS_ERROR) << "Near End when: " << nearEndWhen
			//	<< "; Far End frame read at " << index
			//	<< ", when: " << frame.when << ", delay: " << delay;
			return delay;
		} else {
			// Too old.
			//RTC_LOG(LS_ERROR) << "Near End when: " << nearEndWhen
			//	<< "; ERROR Far End frame too old at " << index
			//	<< ", when: " << frame.when << ", delay: " << delay;
		}
	}
}

AudioDeviceLoopbackWin::AudioDeviceLoopbackWin(
	webrtc::TaskQueueFactory *taskQueueFactory)
: _audioDeviceBuffer(taskQueueFactory)
, _audioProcessing(CreateAudioProcessing())
, _audioSamplesReadyEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr))
, _captureThreadShutdownEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr)) {
}

AudioDeviceLoopbackWin::~AudioDeviceLoopbackWin() {
	Terminate();
	if (_audioSamplesReadyEvent
		&& _audioSamplesReadyEvent != INVALID_HANDLE_VALUE) {
		CloseHandle(_audioSamplesReadyEvent);
	}
	if (_captureThreadShutdownEvent
		&& _captureThreadShutdownEvent != INVALID_HANDLE_VALUE) {
		CloseHandle(_captureThreadShutdownEvent);
	}
}

int32_t AudioDeviceLoopbackWin::ActiveAudioLayer(AudioLayer *audioLayer) const {
	*audioLayer = kPlatformDefaultAudio;
	return 0;
}

int32_t AudioDeviceLoopbackWin::RegisterAudioCallback(
		webrtc::AudioTransport *audioCallback) {
	return _audioDeviceBuffer.RegisterAudioCallback(audioCallback);
}

int32_t AudioDeviceLoopbackWin::Init() {
	if (_initialized) {
		return 0;
	}

	_initialized = true;
	return 0;
}

int32_t AudioDeviceLoopbackWin::Terminate() {
	StopRecording();
	_initialized = false;

	return 0;
}

bool AudioDeviceLoopbackWin::Initialized() const {
	return _initialized;
}

int32_t AudioDeviceLoopbackWin::InitSpeaker() {
	return -1;
}

int32_t AudioDeviceLoopbackWin::InitMicrophone() {
	_microphoneInitialized = true;
	return 0;
}

bool AudioDeviceLoopbackWin::SpeakerIsInitialized() const {
	return false;
}

bool AudioDeviceLoopbackWin::MicrophoneIsInitialized() const {
	return _microphoneInitialized;
}

int32_t AudioDeviceLoopbackWin::SpeakerVolumeIsAvailable(bool *available) {
	if (available) {
		*available = false;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::SetSpeakerVolume(uint32_t volume) {
	return -1;
}

int32_t AudioDeviceLoopbackWin::SpeakerVolume(uint32_t *volume) const {
	return -1;
}

int32_t AudioDeviceLoopbackWin::MaxSpeakerVolume(uint32_t *maxVolume) const {
	return -1;
}

int32_t AudioDeviceLoopbackWin::MinSpeakerVolume(uint32_t *minVolume) const {
	return -1;
}

int32_t AudioDeviceLoopbackWin::SpeakerMuteIsAvailable(bool *available) {
	if (available) {
		*available = false;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::SetSpeakerMute(bool enable) {
	return -1;
}

int32_t AudioDeviceLoopbackWin::SpeakerMute(bool *enabled) const {
	if (enabled) {
		*enabled = false;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::MicrophoneMuteIsAvailable(bool *available) {
	if (available) {
		*available = false;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::SetMicrophoneMute(bool enable) {
	return -1;
}

int32_t AudioDeviceLoopbackWin::MicrophoneMute(bool *enabled) const {
	if (enabled) {
		*enabled = false;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::StereoRecordingIsAvailable(
		bool *available) const {
	if (available) {
		*available = false;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::SetStereoRecording(bool enable) {
	return -1;
}

int32_t AudioDeviceLoopbackWin::StereoRecording(bool *enabled) const {
	if (enabled) {
		*enabled = false;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::StereoPlayoutIsAvailable(bool *available) const {
	if (available) {
		*available = true;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::SetStereoPlayout(bool enable) {
	return enable ? 0 : -1;
}

int32_t AudioDeviceLoopbackWin::StereoPlayout(bool *enabled) const {
	if (enabled) {
		*enabled = true;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::MicrophoneVolumeIsAvailable(
		bool *available) {
	if (available) {
		*available = false;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::SetMicrophoneVolume(uint32_t volume) {
	return -1;
}

int32_t AudioDeviceLoopbackWin::MicrophoneVolume(uint32_t *volume) const {
	return -1;
}

int32_t AudioDeviceLoopbackWin::MaxMicrophoneVolume(uint32_t *maxVolume) const {
	return -1;
}

int32_t AudioDeviceLoopbackWin::MinMicrophoneVolume(uint32_t *minVolume) const {
	return -1;
}

int16_t AudioDeviceLoopbackWin::PlayoutDevices() {
	return 0;
}

int32_t AudioDeviceLoopbackWin::SetPlayoutDevice(uint16_t index) {
	return -1;
}

int32_t AudioDeviceLoopbackWin::SetPlayoutDevice(WindowsDeviceType /*device*/) {
	return -1;
}

int32_t AudioDeviceLoopbackWin::PlayoutDeviceName(
		uint16_t index,
		char name[webrtc::kAdmMaxDeviceNameSize],
		char guid[webrtc::kAdmMaxGuidSize]) {
	return -1;
}

int32_t AudioDeviceLoopbackWin::RecordingDeviceName(
		uint16_t index,
		char name[webrtc::kAdmMaxDeviceNameSize],
		char guid[webrtc::kAdmMaxGuidSize]) {
	if (index != 0) {
		return -1;
	}
	SetStringToArray("System Audio", name, webrtc::kAdmMaxDeviceNameSize);
	SetStringToArray("win_loopback_device_id", guid, webrtc::kAdmMaxGuidSize);
	return 0;
}

int16_t AudioDeviceLoopbackWin::RecordingDevices() {
	return 1;
}

int32_t AudioDeviceLoopbackWin::SetRecordingDevice(uint16_t index) {
	if (index != 0) {
		return -1;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::SetRecordingDevice(WindowsDeviceType device) {
	return 0;
}

int32_t AudioDeviceLoopbackWin::PlayoutIsAvailable(bool *available) {
	if (available) {
		*available = true;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::RecordingIsAvailable(bool *available) {
	if (available) {
		*available = true;
	}
	return 0;
}

int32_t AudioDeviceLoopbackWin::InitPlayout() {
	return -1;
}

void AudioDeviceLoopbackWin::openPlaybackDeviceForCapture() {
	if (_recordingFailed) {
		return;
	}
	auto hr = HRESULT();

	const auto enumerator = winrt::try_create_instance<IMMDeviceEnumerator>(
		__uuidof(MMDeviceEnumerator),
		CLSCTX_ALL);
	if (!enumerator) {
		return captureFailed("Failed to create IMMDeviceEnumerator instance.");
	}
	hr = enumerator->GetDefaultAudioEndpoint(
		eRender,
		eConsole,
		_endpointDevice.put());
	if (FAILED(hr) || !_endpointDevice) {
		return captureFailed("Failed to get default endpoint device.");
	}

	auto state = DWORD();
	hr = _endpointDevice->GetState(&state);
	if (FAILED(hr)) {
		return captureFailed("Failed to get state of the endpoint device.");
	} else if (!(state & DEVICE_STATE_ACTIVE)) {
		return captureFailed("Endpoint device is not active.");
	}
}

void AudioDeviceLoopbackWin::openAudioClient() {
	if (_recordingFailed) {
		return;
	}
	auto hr = HRESULT();

	//WAVEFORMATEXTENSIBLE inputFormat{};

	//const auto format = &inputFormat.Format;
	//format->wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	//format->nChannels = kWantedChannels;
	//format->nSamplesPerSec = kWantedFrequency;
	//format->wBitsPerSample = kWantedBitsPerSample;
	//format->nBlockAlign = (format->wBitsPerSample / 8) * format->nChannels;
	//format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;

	//format->cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	//inputFormat.Samples.wValidBitsPerSample = format->wBitsPerSample;
	//inputFormat.dwChannelMask = KSAUDIO_SPEAKER_MONO;
	//inputFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
	WAVEFORMATEX inputFormat{};

	inputFormat.wFormatTag = WAVE_FORMAT_PCM;
	inputFormat.wBitsPerSample = 16;
	inputFormat.cbSize = 0;
	inputFormat.nChannels = kWantedChannels;
	inputFormat.nSamplesPerSec = kWantedFrequency;
	inputFormat.nBlockAlign = inputFormat.nChannels * inputFormat.wBitsPerSample / 8;
	inputFormat.nAvgBytesPerSec = inputFormat.nSamplesPerSec * inputFormat.nBlockAlign;

	hr = _endpointDevice->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL,
		nullptr,
		_audioClient.put_void());
	if (FAILED(hr) || !_audioClient) {
		return captureFailed("Failed to get IAudioClient.");
	}

	auto closestMatch = (WAVEFORMATEX*)nullptr;
	const auto closestMatchGuard = gsl::finally([&] {
		if (closestMatch) {
			CoTaskMemFree(closestMatch);
		}
	});

	hr = _audioClient->IsFormatSupported(
		AUDCLNT_SHAREMODE_SHARED,
		reinterpret_cast<const WAVEFORMATEX*>(&inputFormat),
		&closestMatch);
	if (FAILED(hr)) {
		return captureFailed("Failed to query IsFormatSupported.");
	} else if (hr != S_OK) {
		if (!closestMatch) {
			return captureFailed("Bad result in IsFormatSupported.");
		}
	} else if (closestMatch) {
		CoTaskMemFree(closestMatch);
		closestMatch = nullptr;
	}
	const auto finalFormat = closestMatch ? closestMatch : &inputFormat;

	_frameSize = finalFormat->nBlockAlign;
	_captureFrequency = finalFormat->nSamplesPerSec;
	_captureFrequencyMultiplier = 10'000'000. / _captureFrequency;
	_captureChannels = finalFormat->nChannels;
	_capturePartFrames = PartSize(_captureFrequency);

	_resampleFrom32 = (finalFormat->wBitsPerSample == 32);

	auto flags = DWORD()
		| AUDCLNT_STREAMFLAGS_LOOPBACK
		| AUDCLNT_STREAMFLAGS_NOPERSIST;
	hr = _audioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		flags,
		100 * 1000 * 10,
		0,
		finalFormat,
		nullptr);
	if (FAILED(hr)) {
		return captureFailed("Failed to initialize IAudioClient.");
	}

	hr = _audioClient->GetBufferSize(&_bufferSizeFrames);
	if (FAILED(hr)) {
		return captureFailed("Failed to get IAudioClient buffer size.");
	}

	hr = _endpointDevice->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL,
		nullptr,
		_audioRenderClientForLoopback.put_void());
	if (FAILED(hr)) {
		return captureFailed("Failed to get render IAudioClient.");
	}

	hr = _audioRenderClientForLoopback->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
		0,
		0,
		reinterpret_cast<const WAVEFORMATEX*>(&inputFormat),
		nullptr);
	if (FAILED(hr)) {
		return captureFailed("Failed to initialize IAudioClient.");
	}

	hr = _audioRenderClientForLoopback->SetEventHandle(
		_audioSamplesReadyEvent);
	if (FAILED(hr)) {
		return captureFailed("Failed to set IAudioClient event handle.");
	}

	hr = _audioClient->GetService(IID_PPV_ARGS(_audioCaptureClient.put()));
	if (FAILED(hr)) {
		return captureFailed("Failed to get IAudioCaptureClient.");
	}
}

void AudioDeviceLoopbackWin::openRecordingDevice() {
	if (_audioCaptureClient) {
		return;
	}

	openPlaybackDeviceForCapture();
	openAudioClient();
}

void AudioDeviceLoopbackWin::captureFailed(const std::string &error) {
	RTC_LOG(LS_ERROR) << "Loopback ADM: " << error;
	_recordingFailed = true;
}

int32_t AudioDeviceLoopbackWin::InitRecording() {
	if (!_initialized
		|| !_audioSamplesReadyEvent
		|| _audioSamplesReadyEvent == INVALID_HANDLE_VALUE) {
		return -1;
	} else if (_recordingInitialized) {
		return 0;
	}
	_recordingInitialized = true;
	openRecordingDevice();
	_audioDeviceBuffer.SetRecordingSampleRate(_captureFrequency);
	_audioDeviceBuffer.SetRecordingChannels(_captureChannels);

	_capturedFrame = std::make_unique<webrtc::AudioFrame>();
	_capturedFrame->sample_rate_hz_ = _captureFrequency;
	_capturedFrame->num_channels_ = _captureChannels;
	_capturedFrame->samples_per_channel_ = _capturePartFrames;

	_renderedFrame = std::make_unique<webrtc::AudioFrame>();
	_renderedFrame->sample_rate_hz_ = kFarEndFrequency;
	_renderedFrame->num_channels_ = kFarEndChannels;
	_renderedFrame->samples_per_channel_ = kFarEndChannelFrameSize;

	LARGE_INTEGER counterFrequency;
	QueryPerformanceFrequency(&counterFrequency);
	if (counterFrequency.QuadPart) {
		_queryPerformanceMultiplier = 10'000'000. / counterFrequency.QuadPart;
	}

	if (_recordingFailed) {
		closeRecordingDevice();
	}
	return 0;
}

void AudioDeviceLoopbackWin::processData() {
	auto hr = HRESULT();

	if (!_recording || _recordingFailed) {
		return;
	}

	BYTE *data = nullptr;
	UINT32 framesAvailable = 0;
	DWORD flags = 0;
	UINT64 position = 0;
	UINT64 counter = 0;

	hr = _audioCaptureClient->GetBuffer(
		&data,
		&framesAvailable,
		&flags,
		&position,
		&counter);

	LARGE_INTEGER counterValue;
	QueryPerformanceCounter(&counterValue);
	const auto nowCounter = (_queryPerformanceMultiplier > 0.)
		? (_queryPerformanceMultiplier * counterValue.QuadPart)
		: 0.;
	const auto now = crl::now();
	const auto whenCaptured = [&] {
		const auto fullDelay = (nowCounter - double(counter))
			+ ((_readSamples - int(framesAvailable))
				* _captureFrequencyMultiplier);
		return now - crl::time(std::round(fullDelay / 10'000.));
	};

	if (FAILED(hr)) {
		captureFailed("Failed call to IAudioCaptureClient::GetBuffer.");
		return;
	} else if (hr == AUDCLNT_S_BUFFER_EMPTY) {
		return;
	} else if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
		data = nullptr;
	}

	if (data) {
		memcpy(
			_syncBuffer.data() + _syncBufferOffset,
			data,
			framesAvailable * _frameSize);
	} else {
		memset(
			_syncBuffer.data() + _syncBufferOffset,
			0,
			framesAvailable * _frameSize);
	}

	hr = _audioCaptureClient->ReleaseBuffer(framesAvailable);
	if (FAILED(hr)) {
		captureFailed("Failed call to IAudioCaptureClient::ReleaseBuffer.");
		return;
	}

	_readSamples += framesAvailable;
	_syncBufferOffset += framesAvailable * _frameSize;

	while (_readSamples >= _capturePartFrames) {
		const auto delay = LoopbackCaptureTakeFarEnd(
			*_renderedFrame,
			whenCaptured());

		if (delay.has_value()) {
			// Testing ideal echo cancellation environment.
			// _audioProcessing->set_stream_delay_ms(0);
			//memcpy(
			//	_capturedFrame->mutable_data(),
			//	_syncBuffer.constData(),
			//	_capturePartFrames * _frameSize);
			//webrtc::ProcessReverseAudioFrame(
			//	_audioProcessing.get(),
			//	_capturedFrame.get());

			_audioProcessing->set_stream_delay_ms(*delay);
			webrtc::ProcessReverseAudioFrame(
				_audioProcessing.get(),
				_renderedFrame.get());

			memcpy(
				_capturedFrame->mutable_data(),
				_syncBuffer.constData(),
				_capturePartFrames * _frameSize);
			webrtc::ProcessAudioFrame(
				_audioProcessing.get(),
				_capturedFrame.get());
			_audioDeviceBuffer.SetRecordedBuffer(
				_capturedFrame->data(),
				_capturePartFrames);
		} else {
			_audioDeviceBuffer.SetRecordedBuffer(
				_syncBuffer.constData(),
				_capturePartFrames);
		}
		_audioDeviceBuffer.DeliverRecordedData();

		memmove(
			_syncBuffer.data(),
			(_syncBuffer.constData() + _capturePartFrames * _frameSize),
			(_readSamples - _capturePartFrames) * _frameSize);
		_readSamples -= _capturePartFrames;
		_syncBufferOffset -= _capturePartFrames * _frameSize;
	}
}

int32_t AudioDeviceLoopbackWin::StartRecording() {
	if (!_recordingInitialized) {
		return -1;
	} else if (_recording) {
		return 0;
	}
	if (_recordingFailed) {
		_recordingFailed = false;
		openRecordingDevice();
	}
	_audioDeviceBuffer.StartRecording();
	startCaptureOnThread();
	return 0;
}


DWORD WINAPI AudioDeviceLoopbackWin::CaptureThreadMethod(LPVOID context) {
	const auto that = reinterpret_cast<AudioDeviceLoopbackWin*>(context);
	return that->runCaptureThread();
}

DWORD AudioDeviceLoopbackWin::runCaptureThread() {
	auto hr = S_OK;

	winrt::init_apartment();
	const auto apartmentGuard = gsl::finally([] {
		winrt::uninit_apartment();
	});

	_syncBuffer.resize(2 * (_bufferSizeFrames * _frameSize));
	_resampleBuffer.resize(2 * (_bufferSizeFrames * _frameSize));
	_syncBufferOffset = 0;

	// hr = InitCaptureThreadPriority(); // #TODO
	if (FAILED(hr)) {
		return hr;
	}

	HANDLE waitArray[] = {
		_captureThreadShutdownEvent,
		_audioSamplesReadyEvent,
	};
	auto interrupted = false;
	while (!interrupted) {
		const auto waitResult = WaitForMultipleObjects(
			2,
			waitArray,
			FALSE,
			INFINITE);
		switch (waitResult) {
		case WAIT_OBJECT_0 + 0: // _captureThreadShutdownEvent
			interrupted = true;
			break;
		case WAIT_OBJECT_0 + 1: // _audioSamplesReadyEvent
			processData();
			break;
		case WAIT_FAILED:
		default:
			captureFailed("Wait failed in capture thread.");
			interrupted = true;
			break;
		}
	}

	//RevertCaptureThreadPriority(); // #TODO

	return (DWORD)hr;
}

void AudioDeviceLoopbackWin::startCaptureOnThread() {
	auto hr = HRESULT();

	LoopbackCaptureActive = true;

	_thread = CreateThread(NULL, 0, CaptureThreadMethod, this, 0, nullptr);
	if (!_thread) {
		return captureFailed("Failed to create thread.");
	}

	SetThreadPriority(_thread, THREAD_PRIORITY_TIME_CRITICAL);

	hr = _audioClient->Start();
	if (FAILED(hr)) {
		return captureFailed("IAudioClient could not Start.");
	}

	hr = _audioRenderClientForLoopback->Start();
	if (FAILED(hr)) {
		return captureFailed("IAudioClient for loopback could not Start.");
	}

	_recording = true;

	if (_recordingFailed) {
		closeRecordingDevice();
	}
}

void AudioDeviceLoopbackWin::stopCaptureOnThread() {
	if (!_recording || !_thread) {
		return;
	}
	_recording = false;
	if (_recordingFailed) {
		return;
	}
	SetEvent(_captureThreadShutdownEvent);
	_audioDeviceBuffer.StopRecording();
	WaitForSingleObject(_thread, INFINITE);

	CloseHandle(_thread);
	_thread = nullptr;
	ResetEvent(_captureThreadShutdownEvent);

	LoopbackCaptureActive = false;
}

int32_t AudioDeviceLoopbackWin::StopRecording() {
	stopCaptureOnThread();
	closeRecordingDevice();
	_recordingInitialized = false;
	return 0;
}

void AudioDeviceLoopbackWin::closeRecordingDevice() {
	ResetEvent(_audioSamplesReadyEvent);
	_audioCaptureClient = nullptr;
	if (_audioRenderClientForLoopback) {
		_audioRenderClientForLoopback->Stop();
	}
	if (_audioClient) {
		_audioClient->Stop();
	}
	_audioRenderClientForLoopback = nullptr;
	_audioClient = nullptr;
	_endpointDevice = nullptr;
}

bool AudioDeviceLoopbackWin::RecordingIsInitialized() const {
	return _recordingInitialized;
}

bool AudioDeviceLoopbackWin::Recording() const {
	return _recording;
}

bool AudioDeviceLoopbackWin::PlayoutIsInitialized() const {
	return false;
}

int32_t AudioDeviceLoopbackWin::StartPlayout() {
	return -1;
}

int32_t AudioDeviceLoopbackWin::StopPlayout() {
	return -1;
}

int32_t AudioDeviceLoopbackWin::PlayoutDelay(uint16_t *delayMS) const {
	if (delayMS) {
		*delayMS = 0;
	}
	return 0;
}

bool AudioDeviceLoopbackWin::BuiltInAECIsAvailable() const {
	return false;
}

bool AudioDeviceLoopbackWin::BuiltInAGCIsAvailable() const {
	return false;
}

bool AudioDeviceLoopbackWin::BuiltInNSIsAvailable() const {
	return false;
}

int32_t AudioDeviceLoopbackWin::EnableBuiltInAEC(bool enable) {
	return enable ? -1 : 0;
}

int32_t AudioDeviceLoopbackWin::EnableBuiltInAGC(bool enable) {
	return enable ? -1 : 0;
}

int32_t AudioDeviceLoopbackWin::EnableBuiltInNS(bool enable) {
	return enable ? -1 : 0;
}

bool AudioDeviceLoopbackWin::Playing() const {
	return false;
}

} // namespace Webrtc::details
