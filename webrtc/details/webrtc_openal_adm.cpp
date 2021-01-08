// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/details/webrtc_openal_adm.h"

#include "base/timer.h"
#include "base/invoke_queued.h"
#include <crl/crl_semaphore.h>

#undef emit
#undef slots
#undef signals

#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
#include <QtCore/QPointer>
#include <QtCore/QThread>

namespace Webrtc::details {
namespace {

constexpr auto kRecordingFrequency = 48000;
constexpr auto kPlayoutFrequency = 48000;
constexpr auto kRecordingChannels = 1;
constexpr auto kPlayoutChannels = 2;
constexpr auto kBufferSizeMs = crl::time(10);
constexpr auto kPlayoutPart = (kPlayoutFrequency * kBufferSizeMs + 999)
	/ 1000;
constexpr auto kPlayoutBufferSize = kPlayoutPart * sizeof(int16_t)
	* kPlayoutChannels;
constexpr auto kRecordingPart = (kRecordingFrequency * kBufferSizeMs + 999)
	/ 1000;
constexpr auto kRecordingBufferSize = kRecordingPart * sizeof(int16_t)
	* kRecordingChannels;
constexpr auto kRestartAfterEmptyData = 50; // Half a second with no data.
constexpr auto kProcessInterval = crl::time(10);

constexpr auto kBuffersFullCount = 10;
constexpr auto kBuffersKeepReadyCount = 8;

constexpr auto kAL_EVENT_CALLBACK_FUNCTION_SOFT = 0x19A2;
constexpr auto kAL_EVENT_CALLBACK_USER_PARAM_SOFT = 0x19A3;
constexpr auto kAL_EVENT_TYPE_BUFFER_COMPLETED_SOFT = 0x19A4;
constexpr auto kAL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT = 0x19A5;
constexpr auto kAL_EVENT_TYPE_DISCONNECTED_SOFT = 0x19A6;

using ALEVENTPROCSOFT = void(*)(
	ALenum eventType,
	ALuint object,
	ALuint param,
	ALsizei length,
	const ALchar *message,
	void *userParam);
using ALEVENTCALLBACKSOFT = void(*)(
	ALEVENTPROCSOFT callback,
	void *userParam);
using ALCSETTHREADCONTEXT = ALCboolean(*)(ALCcontext *context);

ALEVENTCALLBACKSOFT alEventCallbackSOFT/* = nullptr*/;
ALCSETTHREADCONTEXT alcSetThreadContext/* = nullptr*/;

[[nodiscard]] bool Failed(ALCdevice *device) {
	if (auto code = alcGetError(device); code != ALC_NO_ERROR) {
		RTC_LOG(LS_ERROR)
			<< "OpenAL Error "
			<< code
			<< ": "
			<< (const char *)alcGetString(device, code);
		return true;
	}
	return false;
}

template <typename Callback>
void EnumerateDevices(ALCenum specifier, Callback &&callback) {
	auto devices = alcGetString(nullptr, specifier);
	Assert(devices != nullptr);
	while (*devices != 0) {
		callback(devices);
		while (*devices != 0) {
			++devices;
		}
		++devices;
	}
}

[[nodiscard]] int DevicesCount(ALCenum specifier) {
	auto result = 0;
	EnumerateDevices(specifier, [&](const char *device) {
		++result;
	});
	return result;
}

[[nodiscard]] int DeviceName(
		ALCenum specifier,
		int index,
		std::string *name,
		std::string *guid) {
	auto result = 0;
	EnumerateDevices(specifier, [&](const char *device) {
		if (index < 0) {
			return;
		} else if (index > 0) {
			--index;
			return;
		}

		auto string = std::string(device);
		if (name) {
			if (guid) {
				*guid = string;
			}
			const auto prefix = std::string("OpenAL Soft on ");
			if (string.rfind(prefix, 0) == 0) {
				string = string.substr(prefix.size());
			}
			*name = std::move(string);
		} else if (guid) {
			*guid = std::move(string);
		}
		index = -1;
	});
	return (index > 0) ? -1 : 0;
}

void SetStringToArray(const std::string &string, char *array, int size) {
	const auto length = std::min(int(string.size()), size - 1);
	if (length > 0) {
		memcpy(array, string.data(), length);
	}
	array[length] = 0;
}

[[nodiscard]] int DeviceName(
		ALCenum specifier,
		int index,
		char name[webrtc::kAdmMaxDeviceNameSize],
		char guid[webrtc::kAdmMaxGuidSize]) {
	auto sname = std::string();
	auto sguid = std::string();
	const auto result = DeviceName(specifier, index, &sname, &sguid);
	if (result) {
		return result;
	}
	SetStringToArray(sname, name, webrtc::kAdmMaxDeviceNameSize);
	SetStringToArray(sguid, guid, webrtc::kAdmMaxGuidSize);
	return 0;
}

[[nodiscard]] std::string ComputeDefaultDeviceId(ALCenum specifier) {
	const auto device = alcGetString(nullptr, specifier);
	return device ? std::string(device) : std::string();
}

} // namespace

struct AudioDeviceOpenAL::Data {
	Data() : timer(&thread) {
		context.moveToThread(&thread);
	}

	QThread thread;
	QObject context;
	base::Timer timer;

	QByteArray recordedSamples;
	int emptyRecordingData = 0;
	bool recording = false;

	QByteArray playoutSamples;
	ALuint source = 0;
	int queuedBuffersCount = 0;
	std::array<ALuint, kBuffersFullCount> buffers = { { 0 } };
	std::array<bool, kBuffersFullCount> queuedBuffers = { { false } };
	bool playing = false;
};

template <typename Callback>
std::invoke_result_t<Callback> AudioDeviceOpenAL::sync(Callback &&callback) {
	Expects(_data != nullptr);

	using Result = std::invoke_result_t<Callback>;

	crl::semaphore semaphore;
	if constexpr (std::is_same_v<Result, void>) {
		InvokeQueued(&_data->context, [&] {
			callback();
			semaphore.release();
		});
		semaphore.acquire();
	} else {
		auto result = Result();
		InvokeQueued(&_data->context, [&] {
			result = callback();
			semaphore.release();
		});
		semaphore.acquire();
		return result;
	}
}

AudioDeviceOpenAL::AudioDeviceOpenAL(
	webrtc::TaskQueueFactory *taskQueueFactory)
: _audioDeviceBuffer(taskQueueFactory) {
	_audioDeviceBuffer.SetRecordingSampleRate(kRecordingFrequency);
	_audioDeviceBuffer.SetPlayoutSampleRate(kPlayoutFrequency);
	_audioDeviceBuffer.SetRecordingChannels(kRecordingChannels);
	_audioDeviceBuffer.SetPlayoutChannels(kPlayoutChannels);
}

AudioDeviceOpenAL::~AudioDeviceOpenAL() {
	Terminate();
}

int32_t AudioDeviceOpenAL::ActiveAudioLayer(AudioLayer *audioLayer) const {
	*audioLayer = kPlatformDefaultAudio;
	return 0;
}

int32_t AudioDeviceOpenAL::RegisterAudioCallback(
		webrtc::AudioTransport *audioCallback) {
	return _audioDeviceBuffer.RegisterAudioCallback(audioCallback);
}

int32_t AudioDeviceOpenAL::Init() {
	if (_initialized) {
		return 0;
	}
	alcSetThreadContext = (ALCSETTHREADCONTEXT)alcGetProcAddress(
		nullptr,
		"alcSetThreadContext");
	if (!alcSetThreadContext) {
		return -1;
	}
	alEventCallbackSOFT = (ALEVENTCALLBACKSOFT)alcGetProcAddress(
		nullptr,
		"alEventCallbackSOFT");
	_initialized = true;
	return 0;
}

int32_t AudioDeviceOpenAL::Terminate() {
	StopRecording();
	StopPlayout();
	_initialized = false;

	Ensures(!_data);
	return 0;
}

bool AudioDeviceOpenAL::Initialized() const {
	return _initialized;
}

int32_t AudioDeviceOpenAL::InitSpeaker() {
	_speakerInitialized = true;
	return 0;
}

int32_t AudioDeviceOpenAL::InitMicrophone() {
	_microphoneInitialized = true;
	return 0;
}

bool AudioDeviceOpenAL::SpeakerIsInitialized() const {
	return _speakerInitialized;
}

bool AudioDeviceOpenAL::MicrophoneIsInitialized() const {
	return _microphoneInitialized;
}

int32_t AudioDeviceOpenAL::SpeakerVolumeIsAvailable(bool *available) {
	if (available) {
		*available = false;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::SetSpeakerVolume(uint32_t volume) {
	return -1;
}

int32_t AudioDeviceOpenAL::SpeakerVolume(uint32_t *volume) const {
	return -1;
}

int32_t AudioDeviceOpenAL::MaxSpeakerVolume(uint32_t *maxVolume) const {
	return -1;
}

int32_t AudioDeviceOpenAL::MinSpeakerVolume(uint32_t *minVolume) const {
	return -1;
}

int32_t AudioDeviceOpenAL::SpeakerMuteIsAvailable(bool *available) {
	if (available) {
		*available = false;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::SetSpeakerMute(bool enable) {
	return -1;
}

int32_t AudioDeviceOpenAL::SpeakerMute(bool *enabled) const {
	if (enabled) {
		*enabled = false;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::MicrophoneMuteIsAvailable(bool *available) {
	if (available) {
		*available = false;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::SetMicrophoneMute(bool enable) {
	return -1;
}

int32_t AudioDeviceOpenAL::MicrophoneMute(bool *enabled) const {
	if (enabled) {
		*enabled = false;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::StereoRecordingIsAvailable(
		bool *available) const {
	if (available) {
		*available = false;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::SetStereoRecording(bool enable) {
	return -1;
}

int32_t AudioDeviceOpenAL::StereoRecording(bool *enabled) const {
	if (enabled) {
		*enabled = false;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::StereoPlayoutIsAvailable(bool *available) const {
	if (available) {
		*available = true;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::SetStereoPlayout(bool enable) {
	return enable ? 0 : -1;
}

int32_t AudioDeviceOpenAL::StereoPlayout(bool *enabled) const {
	if (enabled) {
		*enabled = true;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::MicrophoneVolumeIsAvailable(
		bool *available) {
	if (available) {
		*available = false;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::SetMicrophoneVolume(uint32_t volume) {
	return -1;
}

int32_t AudioDeviceOpenAL::MicrophoneVolume(uint32_t *volume) const {
	return -1;
}

int32_t AudioDeviceOpenAL::MaxMicrophoneVolume(uint32_t *maxVolume) const {
	return -1;
}

int32_t AudioDeviceOpenAL::MinMicrophoneVolume(uint32_t *minVolume) const {
	return -1;
}

int16_t AudioDeviceOpenAL::PlayoutDevices() {
	return DevicesCount(ALC_ALL_DEVICES_SPECIFIER);
}

int32_t AudioDeviceOpenAL::SetPlayoutDevice(uint16_t index) {
	const auto result = DeviceName(
		ALC_ALL_DEVICES_SPECIFIER,
		index,
		nullptr,
		&_playoutDeviceId);
	return result ? result : restartPlayout();
}

int32_t AudioDeviceOpenAL::SetPlayoutDevice(WindowsDeviceType /*device*/) {
	_playoutDeviceId = ComputeDefaultDeviceId(ALC_DEFAULT_DEVICE_SPECIFIER);
	return _playoutDeviceId.empty() ? -1 : restartPlayout();
}

int32_t AudioDeviceOpenAL::PlayoutDeviceName(
		uint16_t index,
		char name[webrtc::kAdmMaxDeviceNameSize],
		char guid[webrtc::kAdmMaxGuidSize]) {
	return DeviceName(ALC_ALL_DEVICES_SPECIFIER, index, name, guid);
}

int32_t AudioDeviceOpenAL::RecordingDeviceName(
		uint16_t index,
		char name[webrtc::kAdmMaxDeviceNameSize],
		char guid[webrtc::kAdmMaxGuidSize]) {
	return DeviceName(ALC_CAPTURE_DEVICE_SPECIFIER, index, name, guid);
}

int16_t AudioDeviceOpenAL::RecordingDevices() {
	return DevicesCount(ALC_CAPTURE_DEVICE_SPECIFIER);
}

int32_t AudioDeviceOpenAL::SetRecordingDevice(uint16_t index) {
	const auto result = DeviceName(
		ALC_CAPTURE_DEVICE_SPECIFIER,
		index,
		nullptr,
		&_recordingDeviceId);
	return result ? result : restartRecording();
}

int32_t AudioDeviceOpenAL::SetRecordingDevice(WindowsDeviceType /*device*/) {
	_recordingDeviceId = ComputeDefaultDeviceId(
		ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
	return _recordingDeviceId.empty() ? -1 : restartRecording();
}

int32_t AudioDeviceOpenAL::PlayoutIsAvailable(bool *available) {
	if (available) {
		*available = true;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::RecordingIsAvailable(bool *available) {
	if (available) {
		*available = true;
	}
	return 0;
}

int32_t AudioDeviceOpenAL::InitPlayout() {
	if (!_initialized) {
		return -1;
	} else if (_playoutInitialized) {
		return 0;
	}
	_playoutInitialized = true;
	ensureThreadStarted();
	openPlayoutDevice();
	_audioDeviceBuffer.SetPlayoutSampleRate(kPlayoutFrequency);
	_audioDeviceBuffer.SetPlayoutChannels(kPlayoutChannels);
	return 0;
}

void AudioDeviceOpenAL::openRecordingDevice() {
	if (_recordingDevice || _recordingFailed) {
		return;
	}
	_recordingDevice = alcCaptureOpenDevice(
		_recordingDeviceId.empty() ? nullptr : _recordingDeviceId.c_str(),
		kRecordingFrequency,
		AL_FORMAT_MONO16,
		kRecordingFrequency / 4);
	if (!_recordingDevice) {
		RTC_LOG(LS_ERROR)
			<< "OpenAL Capture Device open failed, deviceID: '"
			<< _recordingDeviceId
			<< "'";
		_recordingFailed = true;
		return;
	}
	// This does not work for capture devices :(
	//_context = alcCreateContext(_device, nullptr);
	//	alEventCallbackSOFT([](
	//			ALenum eventType,
	//			ALuint object,
	//			ALuint param,
	//			ALsizei length,
	//			const ALchar *message,
	//			void *that) {
	//		static_cast<AudioInputOpenAL*>(that)->handleEvent(
	//			eventType,
	//			object,
	//			param,
	//			length,
	//			message);
	//	}, this);
}

void AudioDeviceOpenAL::openPlayoutDevice() {
	if (_playoutDevice || _playoutFailed) {
		return;
	}
	_playoutDevice = alcOpenDevice(
		_playoutDeviceId.empty() ? nullptr : _playoutDeviceId.c_str());
	if (!_playoutDevice) {
		RTC_LOG(LS_ERROR)
			<< "OpenAL Device open failed, deviceID: '"
			<< _playoutDeviceId
			<< "'";
		_playoutFailed = true;
		return;
	}
	_playoutContext = alcCreateContext(_playoutDevice, nullptr);
	if (!_playoutContext) {
		RTC_LOG(LS_ERROR) << "OpenAL Context create failed.";
		_playoutFailed = true;
		closePlayoutDevice();
		return;
	}
	sync([&] {
		alcSetThreadContext(_playoutContext);
		if (alEventCallbackSOFT) {
			alEventCallbackSOFT([](
					ALenum eventType,
					ALuint object,
					ALuint param,
					ALsizei length,
					const ALchar *message,
					void *that) {
				static_cast<AudioDeviceOpenAL*>(that)->handleEvent(
					eventType,
					object,
					param,
					length,
					message);
			}, this);
		}
	});
}

void AudioDeviceOpenAL::handleEvent(
		ALenum eventType,
		ALuint object,
		ALuint param,
		ALsizei length,
		const ALchar *message) {
	if (eventType == kAL_EVENT_TYPE_DISCONNECTED_SOFT) {
		const auto weak = QPointer<QObject>(&_data->context);
		_thread->PostTask(RTC_FROM_HERE, [=] {
			if (weak) {
				restartRecording();
			}
		});
	}
}

int32_t AudioDeviceOpenAL::InitRecording() {
	if (!_initialized) {
		return -1;
	} else if (_recordingInitialized) {
		return 0;
	}
	_recordingInitialized = true;
	ensureThreadStarted();
	openRecordingDevice();
	_audioDeviceBuffer.SetRecordingSampleRate(kRecordingFrequency);
	_audioDeviceBuffer.SetRecordingChannels(kRecordingChannels);
	return 0;
}

void AudioDeviceOpenAL::ensureThreadStarted() {
	if (_data) {
		return;
	}
	_thread = rtc::Thread::Current();
	//	Assert(_thread != nullptr);
	//	Assert(_thread->IsOwned());

	_data = std::make_unique<Data>();
	_data->timer.setCallback([=] { processData(); });
	_data->thread.setObjectName("Webrtc OpenAL Thread");
	_data->thread.start(QThread::TimeCriticalPriority);
}

void AudioDeviceOpenAL::processData() {
	Expects(_data != nullptr);

	if (_data->recording && !_recordingFailed) {
		processRecordingData();
	}
	if (_data->playing && !_playoutFailed) {
		processPlayoutData();
	}
}

bool AudioDeviceOpenAL::processRecordedPart(bool firstInCycle) {
	auto samples = ALint();
	alcGetIntegerv(_recordingDevice, ALC_CAPTURE_SAMPLES, 1, &samples);
	if (Failed(_recordingDevice)) {
		restartRecordingQueued();
		return false;
	}
	if (samples <= 0) {
		if (firstInCycle) {
			++_data->emptyRecordingData;
			if (_data->emptyRecordingData == kRestartAfterEmptyData) {
				restartRecordingQueued();
			}
		}
		return false;
	} else if (samples < kRecordingPart) {
		// Not enough data for 10ms.
		return false;
	}

	_data->emptyRecordingData = 0;
	if (_data->recordedSamples.size() < kRecordingBufferSize) {
		_data->recordedSamples.resize(kRecordingBufferSize);
	}
	alcCaptureSamples(
		_recordingDevice,
		_data->recordedSamples.data(),
		kRecordingPart);
	if (Failed(_recordingDevice)) {
		restartRecordingQueued();
		return false;
	}
	_audioDeviceBuffer.SetRecordedBuffer(
		_data->recordedSamples.data(),
		kRecordingPart);
	//_audioDeviceBuffer.SetVQEData(playout_delay_ms_, record_delay_ms);
	_audioDeviceBuffer.DeliverRecordedData();
	return true;
}

void AudioDeviceOpenAL::processRecordingData() {
	for (auto first = true; processRecordedPart(first); first = false) {
	}
}

bool AudioDeviceOpenAL::clearProcessedBuffer() {
	Expects(_data != nullptr);

	auto processed = ALint(0);
	alGetSourcei(_data->source, AL_BUFFERS_PROCESSED, &processed);
	if (processed < 1) {
		return false;
	}
	auto buffer = ALuint(0);
	alSourceUnqueueBuffers(_data->source, 1, &buffer);
	for (auto i = 0; i != int(_data->buffers.size()); ++i) {
		if (_data->buffers[i] == buffer) {
			_data->queuedBuffers[i] = false;
			--_data->queuedBuffersCount;
			return true;
		}
	}
	Unexpected("Processed buffer not found.");
}

void AudioDeviceOpenAL::unqueueAllBuffers() {
	alSourcei(_data->source, AL_BUFFER, AL_NONE);
	ranges::fill(_data->queuedBuffers, false);
	_data->queuedBuffersCount = 0;
}

void AudioDeviceOpenAL::clearProcessedBuffers() {
	while (true) {
		if (!clearProcessedBuffer()) {
			break;
		}
	}
}

void AudioDeviceOpenAL::processPlayoutData() {
	Expects(_data != nullptr);

	const auto playing = [&] {
		auto state = ALint(AL_INITIAL);
		alGetSourcei(_data->source, AL_SOURCE_STATE, &state);
		return (state == AL_PLAYING);
	};
	const auto wasPlaying = playing();
	if (wasPlaying) {
		clearProcessedBuffers();
	} else {
		unqueueAllBuffers();
	}

	const auto wereQueued = _data->queuedBuffers;
	while (_data->queuedBuffersCount < kBuffersKeepReadyCount) {
		const auto available = _audioDeviceBuffer.RequestPlayoutData(
			kPlayoutPart);
		if (available == kPlayoutPart) {
			_audioDeviceBuffer.GetPlayoutData(_data->playoutSamples.data());
		} else {
			//ranges::fill(_data->playoutSamples, 0);
			break;
		}
		const auto i = ranges::find(_data->queuedBuffers, false);
		Assert(i != end(_data->queuedBuffers));
		const auto index = int(i - begin(_data->queuedBuffers));
		alBufferData(
			_data->buffers[index],
			AL_FORMAT_STEREO16,
			_data->playoutSamples.data(),
			_data->playoutSamples.size(),
			kPlayoutFrequency);
		_data->queuedBuffers[index] = true;
		++_data->queuedBuffersCount;
		if (wasPlaying) {
			alSourceQueueBuffers(
				_data->source,
				1,
				_data->buffers.data() + index);
		}
	}
	if (!_data->queuedBuffersCount) {
		return;
	}
	if (!playing()) {
		if (wasPlaying) {
			// While we were queueing buffers the source stopped.
			// Now we can't unqueue only old buffers, so we unqueue all
			// of them and then re-queue the ones we queued right now.
			unqueueAllBuffers();
			for (auto i = 0; i != int(_data->buffers.size()); ++i) {
				if (!wereQueued[i] && _data->queuedBuffers[i]) {
					alSourceQueueBuffers(
						_data->source,
						1,
						_data->buffers.data() + i);
				}
			}
		} else {
			// We were not playing and had no buffers,
			// so queue them all at once.
			alSourceQueueBuffers(
				_data->source,
				_data->queuedBuffersCount,
				_data->buffers.data());
		}
		alSourcePlay(_data->source);
	}

	if (Failed(_playoutDevice)) {
		_playoutFailed = true;
	}
}

int32_t AudioDeviceOpenAL::StartRecording() {
	if (!_recordingInitialized) {
		return -1;
	} else if (_data && _data->recording) {
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

void AudioDeviceOpenAL::startCaptureOnThread() {
	Expects(_data != nullptr);

	sync([&] {
		_data->recording = true;
		if (_recordingFailed) {
			return;
		}
		alcCaptureStart(_recordingDevice);
		if (Failed(_recordingDevice)) {
			_recordingFailed = true;
			return;
		}
		if (!_data->timer.isActive()) {
			_data->timer.callEach(kProcessInterval);
		}
	});
	if (_recordingFailed) {
		closeRecordingDevice();
	}
}

void AudioDeviceOpenAL::stopCaptureOnThread() {
	Expects(_data != nullptr);

	if (!_data->recording) {
		return;
	}
	sync([&] {
		_data->recording = false;
		if (_recordingFailed) {
			return;
		}
		if (!_data->playing) {
			_data->timer.cancel();
		}
		if (_recordingDevice) {
			alcCaptureStop(_recordingDevice);
		}
	});
}

void AudioDeviceOpenAL::startPlayingOnThread() {
	Expects(_data != nullptr);

	sync([&] {
		_data->playing = true;
		if (_playoutFailed) {
			return;
		}
		ALuint source = 0;
		alGenSources(1, &source);
		if (source) {
			alSourcef(source, AL_PITCH, 1.f);
			alSource3f(source, AL_POSITION, 0, 0, 0);
			alSource3f(source, AL_VELOCITY, 0, 0, 0);
			alSourcei(source, AL_LOOPING, 0);
			alSourcei(source, AL_SOURCE_RELATIVE, 1);
			alSourcei(source, AL_ROLLOFF_FACTOR, 0);
			if (alIsExtensionPresent("AL_SOFT_direct_channels_remix")) {
				alSourcei(source, alGetEnumValue("AL_DIRECT_CHANNELS_SOFT"), 2);
			}
			_data->source = source;
			alGenBuffers(_data->buffers.size(), _data->buffers.data());

			_data->playoutSamples = QByteArray(kPlayoutBufferSize, 0);
			//for (auto i = 0; i != kBuffersKeepReadyCount; ++i) {
			//	alBufferData(
			//		_data->buffers[i],
			//		AL_FORMAT_STEREO16,
			//		_data->playoutSamples.data(),
			//		_data->playoutSamples.size(),
			//		kPlayoutFrequency);
			//	_data->queuedBuffers[i] = true;
			//}
			//_data->queuedBuffersCount = kBuffersKeepReadyCount;
			//alSourceQueueBuffers(
			//	source,
			//	kBuffersKeepReadyCount,
			//	_data->buffers.data());
			//alSourcePlay(source);

			if (!_data->timer.isActive()) {
				_data->timer.callEach(kProcessInterval);
			}
		}
	});
}

void AudioDeviceOpenAL::stopPlayingOnThread() {
	Expects(_data != nullptr);

	if (!_data->playing) {
		return;
	}
	sync([&] {
		_data->playing = false;
		if (_playoutFailed) {
			return;
		}
		if (!_data->recording) {
			_data->timer.cancel();
		}
		if (_data->source) {
			alSourceStop(_data->source);
			unqueueAllBuffers();
			alDeleteBuffers(_data->buffers.size(), _data->buffers.data());
			alDeleteSources(1, &_data->source);
			_data->source = 0;
			ranges::fill(_data->buffers, ALuint(0));
		}
	});
}

int32_t AudioDeviceOpenAL::StopRecording() {
	if (_data) {
		stopCaptureOnThread();
		_audioDeviceBuffer.StopRecording();
		if (!_data->playing) {
			_data->thread.quit();
			_data->thread.wait();
			_data = nullptr;
		}
	}
	closeRecordingDevice();
	_recordingInitialized = false;
	return 0;
}

void AudioDeviceOpenAL::restartRecordingQueued() {
	Expects(_data != nullptr);

	if (!_thread || !_thread->IsOwned()) {
		// We support auto-restarting only when started from rtc::Thread.
		return;
	}
	const auto weak = QPointer<QObject>(&_data->context);
	_thread->PostTask(RTC_FROM_HERE, [=] {
		if (weak) {
			restartRecording();
			InvokeQueued(&_data->context, [=] {
				_data->emptyRecordingData = 0;
			});
		}
	});
}

int AudioDeviceOpenAL::restartRecording() {
	if (!_data || !_data->recording) {
		return 0;
	}
	stopCaptureOnThread();
	closeRecordingDevice();
	if (!validateRecordingDeviceId()) {
		sync([&] {
			_data->recording = true;
			_recordingFailed = true;
		});
		return 0;
	}
	_recordingFailed = false;
	openRecordingDevice();
	startCaptureOnThread();
	return 0;
}

void AudioDeviceOpenAL::restartPlayoutQueued() {
	Expects(_data != nullptr);

	if (!_thread || !_thread->IsOwned()) {
		// We support auto-restarting only when started from rtc::Thread.
		return;
	}
	const auto weak = QPointer<QObject>(&_data->context);
	_thread->PostTask(RTC_FROM_HERE, [=] {
		if (weak) {
			restartPlayout();
		}
	});
}

int AudioDeviceOpenAL::restartPlayout() {
	if (!_data || !_data->playing) {
		return 0;
	}
	stopPlayingOnThread();
	closePlayoutDevice();
	if (!validatePlayoutDeviceId()) {
		sync([&] {
			_data->playing = true;
			_playoutFailed = true;
		});
		return 0;
	}
	_playoutFailed = false;
	openPlayoutDevice();
	startPlayingOnThread();
	return 0;
}

void AudioDeviceOpenAL::closeRecordingDevice() {
	//if (_context) {
	//	alcDestroyContext(_context);
	//	_context = nullptr;
	//}
	if (_recordingDevice) {
		alcCaptureCloseDevice(_recordingDevice);
		_recordingDevice = nullptr;
	}
}

void AudioDeviceOpenAL::closePlayoutDevice() {
	if (_playoutContext) {
		alcDestroyContext(_playoutContext);
		_playoutContext = nullptr;
	}
	if (_playoutDevice) {
		alcCaptureCloseDevice(_playoutDevice);
		_playoutDevice = nullptr;
	}
}

bool AudioDeviceOpenAL::validateRecordingDeviceId() {
	auto valid = false;
	EnumerateDevices(ALC_CAPTURE_DEVICE_SPECIFIER, [&](const char *device) {
		if (!valid && _recordingDeviceId == std::string(device)) {
			valid = true;
		}
	});
	if (valid) {
		return true;
	}
	const auto defaultDeviceId = ComputeDefaultDeviceId(
		ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
	if (!defaultDeviceId.empty()) {
		_recordingDeviceId = defaultDeviceId;
		return true;
	}
	RTC_LOG(LS_ERROR) << "Could not find any OpenAL Capture devices.";
	return false;
}

bool AudioDeviceOpenAL::validatePlayoutDeviceId() {
	auto valid = false;
	EnumerateDevices(ALC_ALL_DEVICES_SPECIFIER, [&](const char *device) {
		if (!valid && _playoutDeviceId == std::string(device)) {
			valid = true;
		}
	});
	if (valid) {
		return true;
	}
	const auto defaultDeviceId = ComputeDefaultDeviceId(
		ALC_DEFAULT_DEVICE_SPECIFIER);
	if (!defaultDeviceId.empty()) {
		_playoutDeviceId = defaultDeviceId;
		return true;
	}
	RTC_LOG(LS_ERROR) << "Could not find any OpenAL devices.";
	return false;
}

bool AudioDeviceOpenAL::RecordingIsInitialized() const {
	return _recordingInitialized;
}

bool AudioDeviceOpenAL::Recording() const {
	return _data && _data->recording;
}

bool AudioDeviceOpenAL::PlayoutIsInitialized() const {
	return _playoutInitialized;
}

int32_t AudioDeviceOpenAL::StartPlayout() {
	if (!_playoutInitialized) {
		return -1;
	} else if (Playing()) {
		return 0;
	}
	if (_playoutFailed) {
		_playoutFailed = false;
		openPlayoutDevice();
	}
	_audioDeviceBuffer.StartPlayout();
	startPlayingOnThread();
	return 0;
}

int32_t AudioDeviceOpenAL::StopPlayout() {
	if (_data) {
		stopPlayingOnThread();
		_audioDeviceBuffer.StopPlayout();
		if (!_data->recording) {
			_data->thread.quit();
			_data->thread.wait();
			_data = nullptr;
		}
	}
	closePlayoutDevice();
	_playoutInitialized = false;
	return 0;
}

int32_t AudioDeviceOpenAL::PlayoutDelay(uint16_t *delayMS) const {
	if (delayMS) {
		*delayMS = 0;
	}
	return 0;
}

bool AudioDeviceOpenAL::BuiltInAECIsAvailable() const {
	return false;
}

bool AudioDeviceOpenAL::BuiltInAGCIsAvailable() const {
	return false;
}

bool AudioDeviceOpenAL::BuiltInNSIsAvailable() const {
	return false;
}

int32_t AudioDeviceOpenAL::EnableBuiltInAEC(bool enable) {
	return enable ? -1 : 0;
}

int32_t AudioDeviceOpenAL::EnableBuiltInAGC(bool enable) {
	return enable ? -1 : 0;
}

int32_t AudioDeviceOpenAL::EnableBuiltInNS(bool enable) {
	return enable ? -1 : 0;
}

bool AudioDeviceOpenAL::Playing() const {
	return _data && _data->playing;
}

} // namespace Webrtc::details
