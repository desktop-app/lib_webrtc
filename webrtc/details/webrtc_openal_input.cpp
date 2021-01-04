// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/details/webrtc_openal_input.h"

#include "base/timer.h"
#include "base/invoke_queued.h"

#undef emit
#undef slots
#undef signals

#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
#include "modules/audio_device/audio_device_buffer.h"
#include "modules/audio_device/fine_audio_buffer.h"
#include <QtCore/QPointer>
#include <crl/crl_semaphore.h>
//#include <alext.h>

namespace Webrtc::details {
namespace {

constexpr auto kCaptureFrequency = 48000;
constexpr auto kTakeDataInterval = crl::time(10);
constexpr auto kRestartAfterEmptyTakes = 100;

[[nodiscard]] bool Failed(ALCdevice *device) {
	if (auto code = alcGetError(device); code != ALC_NO_ERROR) {
		RTC_LOG(LS_ERROR)
			<< "OpenAL Capture Error "
			<< code
			<< ": "
			<< (const char *)alcGetString(device, code);
		return true;
	}
	return false;
}

//LPALEVENTCALLBACKSOFT alEventCallbackSOFT/* = nullptr*/;

} // namespace

struct AudioInputOpenAL::Data {
	Data(webrtc::AudioDeviceBuffer *buffer)
	: timer(&thread)
	, fineAudioBuffer(buffer) {
		context.moveToThread(&thread);
	}

	QThread thread;
	QObject context;
	base::Timer timer;
	webrtc::FineAudioBuffer fineAudioBuffer;
	QByteArray samples;
	int emptyTakes = 0;
};

template <typename Callback>
std::invoke_result_t<Callback> AudioInputOpenAL::sync(Callback &&callback) {
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

AudioInputOpenAL::AudioInputOpenAL() {
}

AudioInputOpenAL::~AudioInputOpenAL() {
	StopRecording();
}

int AudioInputOpenAL::Init() {
	return 0;
}

int AudioInputOpenAL::Terminate() {
	if (_device) {
		StopRecording();
	}
	return 0;
}

template <typename Callback>
void AudioInputOpenAL::enumerateDevices(Callback &&callback) const {
	auto devices = alcGetString(nullptr, ALC_CAPTURE_DEVICE_SPECIFIER);
	Assert(devices != nullptr);
	while (*devices != 0) {
		callback(devices);
		while (*devices != 0) {
			++devices;
		}
		++devices;
	}
}

int AudioInputOpenAL::NumDevices() const {
	auto result = 0;
	enumerateDevices([&](const char *device) {
		++result;
	});
	return result;
}

int AudioInputOpenAL::SetDevice(int index) {
	const auto result = DeviceName(index, nullptr, &_deviceId);
	return result ? result : RestartRecording();
}

int AudioInputOpenAL::SetDevice(
		webrtc::AudioDeviceModule::WindowsDeviceType device) {
	_deviceId = computeDefaultDeviceId();
	if (_deviceId.empty()) {
		return -1;
	}
	return RestartRecording();
}

int AudioInputOpenAL::DeviceName(
		int index,
		std::string *name,
		std::string *guid) {
	auto result = 0;
	enumerateDevices([&](const char *device) {
		if (index < 0) {
			return;
		} else if (index > 0) {
			--index;
			return;
		}

		auto string = std::string(device);
		if (guid) {
			*guid = string;
		}
		const auto prefix = std::string("OpenAL Soft on ");
		if (string.rfind(prefix, 0) == 0) {
			string = string.substr(prefix.size());
		}
		if (name) {
			*name = string;
		}
		index = -1;
	});
	return (index > 0) ? -1 : 0;
}

void AudioInputOpenAL::AttachAudioBuffer(
		webrtc::AudioDeviceBuffer* audio_buffer) {
	_audioDeviceBuffer = audio_buffer;
}

bool AudioInputOpenAL::RecordingIsInitialized() const {
	return (_device != nullptr);
}

void AudioInputOpenAL::openDevice() {
	if (_device || _failed) {
		return;
	}
	_device = alcCaptureOpenDevice(
		_deviceId.empty() ? nullptr : _deviceId.c_str(),
		_rate,
		AL_FORMAT_MONO16,
		_rate / 4);
	if (!_device) {
		RTC_LOG(LS_ERROR)
			<< "OpenAL Capture Device open failed, deviceID: '"
			<< _deviceId
			<< "'";
		_failed = true;
		return;
	}
	// This does not work for capture devices :(
	//_context = alcCreateContext(_device, nullptr);
	//if (!_context) {
	//	RTC_LOG(LS_ERROR) << "OpenAL Capture Context create failed.";
	//	_failed = true;
	//	closeDevice();
	//	return;
	//}
	//if (!alEventCallbackSOFT) {
	//	alEventCallbackSOFT = (LPALEVENTCALLBACKSOFT)alcGetProcAddress(
	//		_device,
	//		"alEventCallbackSOFT");
	//}
	//if (alEventCallbackSOFT) {
	//	alcMakeContextCurrent(_context);
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
	//}
}
//
//void AudioInputOpenAL::handleEvent(
//		ALenum eventType,
//		ALuint object,
//		ALuint param,
//		ALsizei length,
//		const ALchar *message) {
//	if (eventType == AL_EVENT_TYPE_DISCONNECTED_SOFT) {
//		const auto weak = QPointer<QObject>(&_data->context);
//		_thread->PostTask(RTC_FROM_HERE, [=] {
//			if (weak) {
//				RestartRecording();
//			}
//		});
//	}
//}

int AudioInputOpenAL::InitRecording() {
	Expects(_audioDeviceBuffer != nullptr);

	if (_data) {
		return 0;
	}
	_rate = _requestedRate
		? _requestedRate
		: kCaptureFrequency;
	openDevice();

	_audioDeviceBuffer->SetRecordingSampleRate(_rate);
	_audioDeviceBuffer->SetRecordingChannels(1);

	_thread = rtc::Thread::Current();
//	Assert(_thread != nullptr);
//	Assert(_thread->IsOwned());

	_data = std::make_unique<Data>(_audioDeviceBuffer);
	_data->timer.setCallback([=] { takeData(); });
	_data->thread.setObjectName("Webrtc OpenAL Capture Thread");
	_data->thread.start(QThread::TimeCriticalPriority);
	return 0;
}

void AudioInputOpenAL::takeData() {
	Expects(_data != nullptr);

	auto samples = ALint();
	alcGetIntegerv(_device, ALC_CAPTURE_SAMPLES, 1, &samples);
	if (Failed(_device)) {
		restartQueued();
		return;
	}
	if (samples <= 0) {
		++_data->emptyTakes;
		if (_data->emptyTakes == kRestartAfterEmptyTakes) {
			restartQueued();
		}
		return;
	}
	_data->emptyTakes = 0;
	const auto byteCount = samples * sizeof(int16_t);
	if (_data->samples.size() < byteCount) {
		_data->samples.resize(byteCount * 2);
	}
	alcCaptureSamples(_device, _data->samples.data(), samples);
	if (Failed(_device)) {
		restartQueued();
		return;
	}
	_data->fineAudioBuffer.DeliverRecordedData(
		rtc::MakeArrayView(
			reinterpret_cast<const int16_t*>(_data->samples.constData()),
			samples),
		0);
}

void AudioInputOpenAL::restartQueued() {
	Expects(_data != nullptr);

	if (!_thread || !_thread->IsOwned()) {
		// We support auto-restarting only when started from rtc::Thread.
		return;
	}
	const auto weak = QPointer<QObject>(&_data->context);
	_thread->PostTask(RTC_FROM_HERE, [=] {
		if (weak) {
			RestartRecording();
			InvokeQueued(&_data->context, [=] {
				_data->emptyTakes = 0;
			});
		}
	});
}

int AudioInputOpenAL::StartRecording() {
	if (_recording) {
		return 0;
	} else if (!_data) {
		RTC_LOG(LS_ERROR) << "OpenAL Capture Device was not opened.";
		return -1;
	}
	_recording = true;
	_audioDeviceBuffer->StartRecording();
	_data->fineAudioBuffer.ResetRecord();
	if (_failed) {
		_failed = false;
		openDevice();
	}
	startCaptureOnThread();
	return 0;
}

void AudioInputOpenAL::startCaptureOnThread() {
	Expects(_data != nullptr);

	if (_failed) {
		return;
	}
	sync([&] {
		alcCaptureStart(_device);
		if (Failed(_device)) {
			_failed = true;
			return;
		}
		_data->timer.callEach(kTakeDataInterval);
	});
	if (_failed) {
		closeDevice();
	}
}

void AudioInputOpenAL::stopCaptureOnThread() {
	Expects(_data != nullptr);

	if (!_recording || _failed) {
		return;
	}
	sync([&] {
		_data->timer.cancel();
		if (_device) {
			alcCaptureStop(_device);
		}
	});
}

int AudioInputOpenAL::StopRecording() {
	if (_data) {
		stopCaptureOnThread();
		_data->thread.quit();
		_data->thread.wait();
		_data = nullptr;
		_audioDeviceBuffer->StopRecording();
	}
	closeDevice();
	_recording = false;
	return 0;
}

void AudioInputOpenAL::closeDevice() {
	//if (_context) {
	//	alcDestroyContext(_context);
	//	_context = nullptr;
	//}
	if (_device) {
		alcCaptureCloseDevice(_device);
		_device = nullptr;
	}
}

bool AudioInputOpenAL::Recording() {
	return _recording;
}

int AudioInputOpenAL::VolumeIsAvailable(bool *available) {
	if (available) {
		*available = false;
	}
	return 0;
}

int AudioInputOpenAL::RestartRecording() {
	if (!_recording) {
		return 0;
	}
	stopCaptureOnThread();
	closeDevice();
	if (!validateDeviceId()) {
		_failed = true;
		return 0;
	}
	_failed = false;
	openDevice();
	startCaptureOnThread();
	return 0;
}

bool AudioInputOpenAL::validateDeviceId() {
	auto valid = false;
	enumerateDevices([&](const char *device) {
		if (!valid && _deviceId == std::string(device)) {
			valid = true;
		}
	});
	if (valid) {
		return true;
	} else if (const auto def = computeDefaultDeviceId(); !def.empty()) {
		_deviceId = def;
		return true;
	}
	RTC_LOG(LS_ERROR) << "Could not find any OpenAL Capture devices.";
	return false;
}

std::string AudioInputOpenAL::computeDefaultDeviceId() const {
	const auto device = alcGetString(
		nullptr,
		ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
	return device ? std::string(device) : std::string();
}

bool AudioInputOpenAL::Restarting() const {
	return false;
}

int AudioInputOpenAL::SetSampleRate(uint32_t sample_rate) {
	_requestedRate = sample_rate;
	return 0;
}

} // namespace Webrtc::details
