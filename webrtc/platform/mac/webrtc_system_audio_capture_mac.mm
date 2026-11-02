// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/platform/mac/webrtc_system_audio_capture_mac.h"

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>

// Avoid AVMediaType name collision between FFmpeg and Apple frameworks.
#define AVMediaType FfmpegAVMediaType
extern "C" {
#include <libswresample/swresample.h>
} // extern "C"
#undef AVMediaType

#include <algorithm>

namespace {

constexpr auto kSampleRate = 48000;
constexpr auto kChannels = 2;

} // namespace

API_AVAILABLE(macos(14.0))
@interface TGSystemAudioCaptureHelper
	: NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithCallback:
	(Webrtc::SystemAudioSamplesCallback)callback;
- (void)startCapture;
- (void)stopCapture;
@end

@implementation TGSystemAudioCaptureHelper {
	Webrtc::SystemAudioSamplesCallback _callback;
	SCStream *_stream;
	dispatch_queue_t _audioQueue;
	std::atomic<bool> _active;
	SwrContext *_swrContext;
	int _swrSrcRate;
	int _swrSrcChannels;
	bool _swrSrcPlanar;
}

- (instancetype)initWithCallback:
		(Webrtc::SystemAudioSamplesCallback)callback {
	self = [super init];
	if (self) {
		_callback = std::move(callback);
		_active = false;
		_swrContext = nullptr;
		_audioQueue = dispatch_queue_create(
			"org.telegram.desktop.SystemAudioCapture",
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
		_active = false;
		[stream release];
		return;
	}
	_stream = stream;

	auto __weak weakCapture = self;
	[stream startCaptureWithCompletionHandler:^(NSError *startError) {
		if (startError) {
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
		if (self->_swrContext) {
			swr_free(&self->_swrContext);
		}
	});
	dispatch_semaphore_wait(
		sem,
		dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
	// Even on timeout, an entered sample must not outlive the owner's callback.
	dispatch_sync(_audioQueue, ^{
	});
	dispatch_release(sem);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
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
		kSampleRate,
		&srcLayout,
		srcFormat,
		srcRate,
		0,
		nullptr);
	if (result < 0 || !_swrContext || swr_init(_swrContext) < 0) {
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

	auto outBuf = std::vector<uint8_t>(
		maxOut * kChannels * sizeof(int16_t));
	auto *outData = outBuf.data();

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
		return;
	}

	outBuf.resize(samplesOut * kChannels * sizeof(int16_t));
	_callback(std::move(outBuf));
}

@end

namespace Webrtc::details {

struct SystemAudioCaptureMac::Impl {
	id helper = nil; // TGSystemAudioCaptureHelper
};

SystemAudioCaptureMac::SystemAudioCaptureMac(
		SystemAudioSamplesCallback callback)
: _impl(std::make_unique<Impl>()) {
	if (@available(macOS 14.0, *)) {
		_impl->helper = [[TGSystemAudioCaptureHelper alloc]
			initWithCallback:std::move(callback)];
	}
}

SystemAudioCaptureMac::~SystemAudioCaptureMac() {
	stop();
	[_impl->helper release];
}

bool SystemAudioCaptureMac::IsSupported() {
	if (@available(macOS 14.0, *)) {
		return true;
	}
	return false;
}

void SystemAudioCaptureMac::start() {
	if (@available(macOS 14.0, *)) {
		[_impl->helper startCapture];
	}
}

void SystemAudioCaptureMac::stop() {
	if (@available(macOS 14.0, *)) {
		[_impl->helper stopCapture];
	}
}

} // namespace Webrtc::details
