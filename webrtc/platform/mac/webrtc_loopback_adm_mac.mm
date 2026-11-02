// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/platform/mac/webrtc_loopback_adm_mac.h"

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>

// Avoid AVMediaType name collision between FFmpeg and Apple frameworks.
#define AVMediaType FfmpegAVMediaType
extern "C" {
#include <libswresample/swresample.h>
} // extern "C"
#undef AVMediaType

#include <rtc_base/logging.h>

#include <algorithm>

namespace Webrtc::details {
namespace {

constexpr auto kSampleRate = 48000;
constexpr auto kChannels = 2;
constexpr auto kBufferSizeMs = 10;
constexpr auto kSamplesPerChannel10Ms = kSampleRate * kBufferSizeMs / 1000;
// 480 samples per channel per 10ms frame.
static_assert(kSamplesPerChannel10Ms == 480);

void SetStringToArray(const std::string &string, char *array, int size) {
	const auto length = std::min(int(string.size()), size - 1);
	if (length > 0) {
		memcpy(array, string.data(), length);
	}
	array[length] = 0;
}

} // namespace
} // namespace Webrtc::details

API_AVAILABLE(macos(14.0))
@interface TGLoopbackAudioHelper
	: NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithAudioDeviceBuffer:
	(webrtc::AudioDeviceBuffer *)buffer;
- (void)startCapture;
- (void)stopCapture;
@end

@implementation TGLoopbackAudioHelper {
	webrtc::AudioDeviceBuffer *_audioDeviceBuffer;
	SCStream *_stream;
	dispatch_queue_t _audioQueue;
	std::atomic<bool> _active;
	std::vector<int16_t> _accumBuffer;
	SwrContext *_swrContext;
	int _swrSrcRate;
	int _swrSrcChannels;
	bool _swrSrcPlanar;
}

- (instancetype)initWithAudioDeviceBuffer:
		(webrtc::AudioDeviceBuffer *)buffer {
	self = [super init];
	if (self) {
		_audioDeviceBuffer = buffer;
		_active = false;
		_swrContext = nullptr;
		_audioQueue = dispatch_queue_create(
			"org.telegram.desktop.LoopbackAudioCapture",
			DISPATCH_QUEUE_SERIAL);
	}
	return self;
}

- (void)dealloc {
	[_stream release];
	if (_swrContext) {
		swr_free(&_swrContext);
	}
	dispatch_release(_audioQueue);
	[super dealloc];
}

- (void)startCapture {
	if (_active.exchange(true)) {
		return;
	}

	auto __weak weakSelf = self;
	[SCShareableContent
		getShareableContentExcludingDesktopWindows:YES
		onScreenWindowsOnly:NO
		completionHandler:^(SCShareableContent *content, NSError *error) {
			auto strongSelf = weakSelf;
			if (!strongSelf || !strongSelf->_active) {
				return;
			}
			if (error || !content.displays.count) {
				strongSelf->_active = false;
				return;
			}
			dispatch_async(strongSelf->_audioQueue, ^{
				auto inner = weakSelf;
				if (!inner || !inner->_active) {
					return;
				}
				[inner setupStreamWithContent:content];
			});
		}];
}

- (void)setupStreamWithContent:(SCShareableContent *)content {
	using namespace Webrtc::details;

	SCDisplay *display = content.displays.firstObject;
	SCContentFilter *filter = [[SCContentFilter alloc]
		initWithDisplay:display
		excludingApplications:@[]
		exceptingWindows:@[]];

	SCStreamConfiguration *config =
		[[SCStreamConfiguration alloc] init];
	config.capturesAudio = YES;
	config.excludesCurrentProcessAudio = YES;
	config.sampleRate = kSampleRate;
	config.channelCount = kChannels;
	// Minimize video overhead.
	config.width = 2;
	config.height = 2;
	config.minimumFrameInterval = CMTimeMake(1, 1);

	if (_stream) {
		auto previous = base::take(_stream);
		[previous stopCaptureWithCompletionHandler:^(NSError *error) {
			[previous release];
		}];
	}
	auto stream = [[SCStream alloc]
		initWithFilter:filter
		configuration:config
		delegate:self];
	[filter release];
	[config release];

	NSError *addError = nil;
	[stream addStreamOutput:self
		type:SCStreamOutputTypeAudio
		sampleHandlerQueue:_audioQueue
		error:&addError];
	if (addError) {
		RTC_LOG(LS_ERROR) << "Loopback ADM Mac: "
			<< "failed to add stream output.";
		_active = false;
		[stream release];
		return;
	}
	_stream = stream;

	auto __weak weakCapture = self;
	[stream startCaptureWithCompletionHandler:^(NSError *startError) {
		if (startError) {
			RTC_LOG(LS_ERROR) << "Loopback ADM Mac: "
				<< "failed to start capture.";
			if (auto strong = weakCapture) {
				dispatch_async(strong->_audioQueue, ^{
					auto s = weakCapture;
					if (s && s->_stream == stream) {
						s->_active = false;
						[base::take(s->_stream) release];
					}
				});
			}
		}
	}];
}

- (void)stopCapture {
	_active = false;
	dispatch_semaphore_t sem = dispatch_semaphore_create(0);
	dispatch_async(_audioQueue, ^{
		if (self->_stream) {
			auto stream = base::take(self->_stream);
			[stream stopCaptureWithCompletionHandler:^(NSError *error) {
				[stream release];
				dispatch_semaphore_signal(sem);
			}];
		} else {
			dispatch_semaphore_signal(sem);
		}
		self->_accumBuffer.clear();
		if (self->_swrContext) {
			swr_free(&self->_swrContext);
		}
	});
	dispatch_semaphore_wait(
		sem,
		dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
	// WHY: a sample callback already entered still uses _audioDeviceBuffer,
	// which the owner destroys right after this returns, so it has to finish
	// here even after a timed out wait.
	dispatch_sync(_audioQueue, ^{
	});
	dispatch_release(sem);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
	RTC_LOG(LS_ERROR) << "Loopback ADM Mac: stream stopped with error.";
	_active = false;
}

- (bool)ensureResamplerWithRate:(int)srcRate
		channels:(int)srcChannels
		planar:(bool)planar {
	if (_swrContext
		&& _swrSrcRate == srcRate
		&& _swrSrcChannels == srcChannels
		&& _swrSrcPlanar == planar) {
		return true;
	}
	if (_swrContext) {
		swr_free(&_swrContext);
	}

	const auto srcFormat = planar
		? AV_SAMPLE_FMT_FLTP
		: AV_SAMPLE_FMT_FLT;
	const auto srcLayout = (srcChannels >= 2)
		? AVChannelLayout(AV_CHANNEL_LAYOUT_STEREO)
		: AVChannelLayout(AV_CHANNEL_LAYOUT_MONO);
	const auto dstLayout = AVChannelLayout(AV_CHANNEL_LAYOUT_STEREO);

	const auto result = swr_alloc_set_opts2(
		&_swrContext,
		&dstLayout,
		AV_SAMPLE_FMT_S16,
		Webrtc::details::kSampleRate,
		&srcLayout,
		srcFormat,
		srcRate,
		0,
		nullptr);
	if (result < 0 || !_swrContext || swr_init(_swrContext) < 0) {
		RTC_LOG(LS_ERROR) << "Loopback ADM Mac: "
			<< "failed to init resampler for rate " << srcRate;
		if (_swrContext) {
			swr_free(&_swrContext);
		}
		return false;
	}

	_swrSrcRate = srcRate;
	_swrSrcChannels = srcChannels;
	_swrSrcPlanar = planar;
	return true;
}

- (void)stream:(SCStream *)stream
		didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
		ofType:(SCStreamOutputType)type {
	using namespace Webrtc::details;

	if (type != SCStreamOutputTypeAudio || !_active) {
		return;
	}

	const auto formatDesc =
		CMSampleBufferGetFormatDescription(sampleBuffer);
	if (!formatDesc) {
		return;
	}
	const auto asbd =
		CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
	if (!asbd || !(asbd->mFormatFlags & kAudioFormatFlagIsFloat)) {
		return;
	}

	const auto numSamples =
		(int)CMSampleBufferGetNumSamples(sampleBuffer);
	if (numSamples <= 0) {
		return;
	}

	const auto srcRate = (int)asbd->mSampleRate;
	const auto srcChannels = (int)asbd->mChannelsPerFrame;
	const auto planar =
		(asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;

	if (![self ensureResamplerWithRate:srcRate
			channels:srcChannels
			planar:planar]) {
		return;
	}

	const auto maxOut = (int)av_rescale_rnd(
		swr_get_delay(_swrContext, srcRate) + numSamples,
		kSampleRate,
		srcRate,
		AV_ROUND_UP);

	const auto prevSize = _accumBuffer.size();
	_accumBuffer.resize(prevSize + maxOut * kChannels);
	auto *outData = reinterpret_cast<uint8_t *>(
		_accumBuffer.data() + prevSize);

	int samplesOut = 0;

	if (planar) {
		constexpr auto kListSize = sizeof(AudioBufferList)
			+ sizeof(AudioBuffer);
		char listBuf[kListSize] = {};
		auto *list = reinterpret_cast<AudioBufferList *>(listBuf);

		CMBlockBufferRef bb = nil;
		const auto status =
			CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
				sampleBuffer,
				nullptr,
				list,
				kListSize,
				nullptr,
				nullptr,
				kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
				&bb);
		if (status != noErr || !bb) {
			_accumBuffer.resize(prevSize);
			return;
		}

		const uint8_t *inPlanes[2];
		inPlanes[0] =
			static_cast<const uint8_t *>(list->mBuffers[0].mData);
		inPlanes[1] = (srcChannels >= 2)
			? static_cast<const uint8_t *>(list->mBuffers[1].mData)
			: inPlanes[0];

		samplesOut = swr_convert(
			_swrContext,
			&outData,
			maxOut,
			inPlanes,
			numSamples);

		CFRelease(bb);
	} else {
		auto *bb = CMSampleBufferGetDataBuffer(sampleBuffer);
		if (!bb) {
			_accumBuffer.resize(prevSize);
			return;
		}

		size_t totalLength = 0;
		char *dataPtr = nullptr;
		CMBlockBufferGetDataPointer(
			bb,
			0,
			nullptr,
			&totalLength,
			&dataPtr);
		if (!dataPtr || !totalLength) {
			_accumBuffer.resize(prevSize);
			return;
		}

		const uint8_t *inPlanes[1] = {
			reinterpret_cast<const uint8_t *>(dataPtr),
		};
		samplesOut = swr_convert(
			_swrContext,
			&outData,
			maxOut,
			inPlanes,
			numSamples);
	}

	if (samplesOut <= 0) {
		_accumBuffer.resize(prevSize);
		return;
	}

	_accumBuffer.resize(prevSize + samplesOut * kChannels);

	// Cap buffer at 1 second to prevent unbounded growth.
	constexpr auto kMaxBufferSamples = kSampleRate * kChannels;
	if (_accumBuffer.size() > kMaxBufferSamples) {
		_accumBuffer.clear();
		return;
	}

	// Deliver in 10ms chunks (480 samples per channel, 960 total).
	constexpr auto kChunkSamples = kSamplesPerChannel10Ms * kChannels;
	auto offset = size_t(0);
	while (offset + kChunkSamples <= _accumBuffer.size()) {
		_audioDeviceBuffer->SetRecordedBuffer(
			_accumBuffer.data() + offset,
			kSamplesPerChannel10Ms);
		_audioDeviceBuffer->DeliverRecordedData();
		offset += kChunkSamples;
	}

	// Keep remaining samples.
	if (offset > 0) {
		const auto remaining = _accumBuffer.size() - offset;
		if (remaining > 0) {
			memmove(
				_accumBuffer.data(),
				_accumBuffer.data() + offset,
				remaining * sizeof(int16_t));
		}
		_accumBuffer.resize(remaining);
	}
}

@end

namespace Webrtc::details {

struct AudioDeviceLoopbackMac::ObjCState {
	id helper = nil; // TGLoopbackAudioHelper
};

AudioDeviceLoopbackMac::AudioDeviceLoopbackMac(
		webrtc::TaskQueueFactory *taskQueueFactory)
: _audioDeviceBuffer(taskQueueFactory)
, _objc(std::make_unique<ObjCState>()) {
}

AudioDeviceLoopbackMac::~AudioDeviceLoopbackMac() {
	Terminate();
}

int32_t AudioDeviceLoopbackMac::ActiveAudioLayer(
		AudioLayer *audioLayer) const {
	*audioLayer = kPlatformDefaultAudio;
	return 0;
}

int32_t AudioDeviceLoopbackMac::RegisterAudioCallback(
		webrtc::AudioTransport *audioCallback) {
	return _audioDeviceBuffer.RegisterAudioCallback(audioCallback);
}

int32_t AudioDeviceLoopbackMac::Init() {
	if (_initialized) {
		return 0;
	}
	_initialized = true;
	return 0;
}

int32_t AudioDeviceLoopbackMac::Terminate() {
	StopRecording();
	_initialized = false;
	return 0;
}

bool AudioDeviceLoopbackMac::Initialized() const {
	return _initialized;
}

int32_t AudioDeviceLoopbackMac::InitSpeaker() {
	return -1;
}

int32_t AudioDeviceLoopbackMac::InitMicrophone() {
	_microphoneInitialized = true;
	return 0;
}

bool AudioDeviceLoopbackMac::SpeakerIsInitialized() const {
	return false;
}

bool AudioDeviceLoopbackMac::MicrophoneIsInitialized() const {
	return _microphoneInitialized;
}

int32_t AudioDeviceLoopbackMac::SpeakerVolumeIsAvailable(bool *available) {
	if (available) *available = false;
	return 0;
}

int32_t AudioDeviceLoopbackMac::SetSpeakerVolume(uint32_t volume) {
	return -1;
}

int32_t AudioDeviceLoopbackMac::SpeakerVolume(uint32_t *volume) const {
	return -1;
}

int32_t AudioDeviceLoopbackMac::MaxSpeakerVolume(
		uint32_t *maxVolume) const {
	return -1;
}

int32_t AudioDeviceLoopbackMac::MinSpeakerVolume(
		uint32_t *minVolume) const {
	return -1;
}

int32_t AudioDeviceLoopbackMac::SpeakerMuteIsAvailable(bool *available) {
	if (available) *available = false;
	return 0;
}

int32_t AudioDeviceLoopbackMac::SetSpeakerMute(bool enable) {
	return -1;
}

int32_t AudioDeviceLoopbackMac::SpeakerMute(bool *enabled) const {
	if (enabled) *enabled = false;
	return 0;
}

int32_t AudioDeviceLoopbackMac::MicrophoneMuteIsAvailable(bool *available) {
	if (available) *available = false;
	return 0;
}

int32_t AudioDeviceLoopbackMac::SetMicrophoneMute(bool enable) {
	return -1;
}

int32_t AudioDeviceLoopbackMac::MicrophoneMute(bool *enabled) const {
	if (enabled) *enabled = false;
	return 0;
}

int32_t AudioDeviceLoopbackMac::StereoRecordingIsAvailable(
		bool *available) const {
	if (available) *available = false;
	return 0;
}

int32_t AudioDeviceLoopbackMac::SetStereoRecording(bool enable) {
	return -1;
}

int32_t AudioDeviceLoopbackMac::StereoRecording(bool *enabled) const {
	if (enabled) *enabled = false;
	return 0;
}

int32_t AudioDeviceLoopbackMac::StereoPlayoutIsAvailable(
		bool *available) const {
	if (available) *available = true;
	return 0;
}

int32_t AudioDeviceLoopbackMac::SetStereoPlayout(bool enable) {
	return enable ? 0 : -1;
}

int32_t AudioDeviceLoopbackMac::StereoPlayout(bool *enabled) const {
	if (enabled) *enabled = true;
	return 0;
}

int32_t AudioDeviceLoopbackMac::MicrophoneVolumeIsAvailable(
		bool *available) {
	if (available) *available = false;
	return 0;
}

int32_t AudioDeviceLoopbackMac::SetMicrophoneVolume(uint32_t volume) {
	return -1;
}

int32_t AudioDeviceLoopbackMac::MicrophoneVolume(uint32_t *volume) const {
	return -1;
}

int32_t AudioDeviceLoopbackMac::MaxMicrophoneVolume(
		uint32_t *maxVolume) const {
	return -1;
}

int32_t AudioDeviceLoopbackMac::MinMicrophoneVolume(
		uint32_t *minVolume) const {
	return -1;
}

int16_t AudioDeviceLoopbackMac::PlayoutDevices() {
	return 0;
}

int32_t AudioDeviceLoopbackMac::SetPlayoutDevice(uint16_t index) {
	return -1;
}

int32_t AudioDeviceLoopbackMac::SetPlayoutDevice(
		WindowsDeviceType /*device*/) {
	return -1;
}

int32_t AudioDeviceLoopbackMac::PlayoutDeviceName(
		uint16_t index,
		char name[webrtc::kAdmMaxDeviceNameSize],
		char guid[webrtc::kAdmMaxGuidSize]) {
	return -1;
}

int32_t AudioDeviceLoopbackMac::RecordingDeviceName(
		uint16_t index,
		char name[webrtc::kAdmMaxDeviceNameSize],
		char guid[webrtc::kAdmMaxGuidSize]) {
	if (index != 0) {
		return -1;
	}
	SetStringToArray(
		"System Audio",
		name,
		webrtc::kAdmMaxDeviceNameSize);
	SetStringToArray(
		"mac_sck_loopback_device_id",
		guid,
		webrtc::kAdmMaxGuidSize);
	return 0;
}

int16_t AudioDeviceLoopbackMac::RecordingDevices() {
	return 1;
}

int32_t AudioDeviceLoopbackMac::SetRecordingDevice(uint16_t index) {
	return (index == 0) ? 0 : -1;
}

int32_t AudioDeviceLoopbackMac::SetRecordingDevice(
		WindowsDeviceType device) {
	return 0;
}

int32_t AudioDeviceLoopbackMac::PlayoutIsAvailable(bool *available) {
	if (available) *available = false;
	return 0;
}

int32_t AudioDeviceLoopbackMac::RecordingIsAvailable(bool *available) {
	if (available) *available = true;
	return 0;
}

int32_t AudioDeviceLoopbackMac::InitPlayout() {
	return -1;
}

int32_t AudioDeviceLoopbackMac::InitRecording() {
	if (!_initialized) {
		return -1;
	}
	if (_recordingInitialized) {
		return 0;
	}
	_recordingInitialized = true;
	_audioDeviceBuffer.SetRecordingSampleRate(kSampleRate);
	_audioDeviceBuffer.SetRecordingChannels(kChannels);

	if (@available(macOS 14.0, *)) {
		_objc->helper = [[TGLoopbackAudioHelper alloc]
			initWithAudioDeviceBuffer:&_audioDeviceBuffer];
	}
	return 0;
}

int32_t AudioDeviceLoopbackMac::StartRecording() {
	if (!_recordingInitialized) {
		return -1;
	}
	if (_recording) {
		return 0;
	}
	_recording = true;
	_audioDeviceBuffer.StartRecording();
	if (@available(macOS 14.0, *)) {
		[_objc->helper startCapture];
	}
	return 0;
}

int32_t AudioDeviceLoopbackMac::StopRecording() {
	if (!_recording) {
		_recordingInitialized = false;
		[base::take(_objc->helper) release];
		return 0;
	}
	_recording = false;
	if (@available(macOS 14.0, *)) {
		[_objc->helper stopCapture];
		[base::take(_objc->helper) release];
	}
	_audioDeviceBuffer.StopRecording();
	_recordingInitialized = false;
	return 0;
}

bool AudioDeviceLoopbackMac::RecordingIsInitialized() const {
	return _recordingInitialized;
}

bool AudioDeviceLoopbackMac::Recording() const {
	return _recording;
}

bool AudioDeviceLoopbackMac::PlayoutIsInitialized() const {
	return false;
}

int32_t AudioDeviceLoopbackMac::StartPlayout() {
	return -1;
}

int32_t AudioDeviceLoopbackMac::StopPlayout() {
	return -1;
}

bool AudioDeviceLoopbackMac::Playing() const {
	return false;
}

int32_t AudioDeviceLoopbackMac::PlayoutDelay(uint16_t *delayMS) const {
	if (delayMS) *delayMS = 0;
	return 0;
}

bool AudioDeviceLoopbackMac::BuiltInAECIsAvailable() const {
	return false;
}

bool AudioDeviceLoopbackMac::BuiltInAGCIsAvailable() const {
	return false;
}

bool AudioDeviceLoopbackMac::BuiltInNSIsAvailable() const {
	return false;
}

int32_t AudioDeviceLoopbackMac::EnableBuiltInAEC(bool enable) {
	return -1;
}

int32_t AudioDeviceLoopbackMac::EnableBuiltInAGC(bool enable) {
	return -1;
}

int32_t AudioDeviceLoopbackMac::EnableBuiltInNS(bool enable) {
	return -1;
}

bool AudioDeviceLoopbackMac::IsSupported() {
	if (@available(macOS 14.0, *)) {
		return true;
	}
	return false;
}

} // namespace Webrtc::details
