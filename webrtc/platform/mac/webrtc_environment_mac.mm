// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/platform/mac/webrtc_environment_mac.h"

#include "base/weak_ptr.h"
#include "webrtc/webrtc_environment.h"
#include "webrtc/webrtc_create_adm.h"

#include <modules/audio_device/include/audio_device_defines.h>
#include <modules/audio_device/include/audio_device.h>
#include <api/task_queue/default_task_queue_factory.h>

#import <AVFoundation/AVFoundation.h>
#import <IOKit/hidsystem/IOHIDLib.h>
#import <CoreAudio/CoreAudio.h>
#import <Cocoa/Cocoa.h>

@interface InputMuteObserver : NSObject {
}

- (id) init;
- (void) inputMuteStateChange:(NSNotification *)aNotification;

@end // @interface InputMuteObserver

@implementation InputMuteObserver {
}

- (id) init {
	if (self = [super init]) {
	}
	return self;
}

- (void) inputMuteStateChange:(NSNotification *)aNotification {
}

@end // @implementation InputMuteObserver

namespace Webrtc::Platform {
namespace {

constexpr auto kMaxNameLength = 256;

class PropertyMonitor {
public:
	using Method = void (EnvironmentMac::*)();

	PropertyMonitor(
		AudioObjectPropertyAddress address,
		Method method);

	void registerEnvironment(not_null<EnvironmentMac*> environment);
	void unregisterEnvironment(not_null<EnvironmentMac*> environment);

private:
	void subscribe();
	void unsubscribe();
	void process();

	static OSStatus Callback(
		AudioObjectID inObjectID,
		UInt32 inNumberAddresses,
		const AudioObjectPropertyAddress *inAddresses,
		void *inClientData);

	const AudioObjectPropertyAddress _address = {};
	Method _method = nullptr;
	std::vector<not_null<EnvironmentMac*>> _list;

};

auto DefaultPlaybackDeviceChangedMonitor = PropertyMonitor({
	.mSelector = kAudioHardwarePropertyDefaultOutputDevice,
	.mScope = kAudioObjectPropertyScopeGlobal,
	.mElement = kAudioObjectPropertyElementMain,
}, &EnvironmentMac::defaultPlaybackDeviceChanged);

auto DefaultCaptureDeviceChangedMonitor = PropertyMonitor({
	.mSelector = kAudioHardwarePropertyDefaultInputDevice,
	.mScope = kAudioObjectPropertyScopeGlobal,
	.mElement = kAudioObjectPropertyElementMain,
}, &EnvironmentMac::defaultCaptureDeviceChanged);

auto AudioDeviceListChangedMonitor = PropertyMonitor({
	.mSelector = kAudioHardwarePropertyDevices,
	.mScope = kAudioObjectPropertyScopeGlobal,
	.mElement = kAudioObjectPropertyElementMain,
}, &EnvironmentMac::audioDeviceListChanged);

PropertyMonitor::PropertyMonitor(
	AudioObjectPropertyAddress address,
	Method method)
: _address(address)
, _method(method) {
}

void PropertyMonitor::registerEnvironment(
		not_null<EnvironmentMac*> environment) {
	if (empty(_list)) {
		subscribe();
	}
	_list.push_back(environment);
}

void PropertyMonitor::unregisterEnvironment(
		not_null<EnvironmentMac*> environment) {
	_list.erase(ranges::remove(_list, environment), end(_list));
	if (empty(_list)) {
		unsubscribe();
	}
}

void PropertyMonitor::subscribe() {
	AudioObjectAddPropertyListener(
		kAudioObjectSystemObject,
		&_address,
		&PropertyMonitor::Callback,
		this);
}

void PropertyMonitor::unsubscribe() {
	AudioObjectRemovePropertyListener(
		kAudioObjectSystemObject,
		&_address,
		&PropertyMonitor::Callback,
		this);
}

void PropertyMonitor::process() {
	for (auto i = 0; i < _list.size(); ++i) {
		(_list[i]->*_method)();
	}
}

OSStatus PropertyMonitor::Callback(
		AudioObjectID inObjectID,
		UInt32 inNumberAddresses,
		const AudioObjectPropertyAddress *inAddresses,
		void *inClientData) {
	const auto monitor = static_cast<PropertyMonitor*>(inClientData);
	crl::on_main([monitor] {
		monitor->process();
	});
	return 0;
}

[[nodiscard]] QString CFStringToQString(CFStringRef text) {
	if (!text) {
		return QString();
	}
	const auto length = CFStringGetLength(text);
	const auto bytes = length * sizeof(QChar);
	auto result = QString(length, QChar(0));
	auto used = CFIndex(0);
	const auto converted = CFStringGetBytes(
		text,
		CFRange{ 0, length },
		kCFStringEncodingUTF16LE,
		UInt8(),
		false,
		reinterpret_cast<UInt8*>(result.data()),
		bytes,
		&used);
	if (!converted || !used || (used % sizeof(QChar))) {
		return QString();
	} else if (used < bytes) {
		result.resize(used / sizeof(QChar));
	}
	return result;
}

[[nodiscard]] QString GetDeviceUID(AudioDeviceID deviceId) {
	if (deviceId == kAudioDeviceUnknown) {
		return QString();
	}
	auto uid = CFStringRef(nullptr);
	const auto guard = gsl::finally([&] {
		if (uid) {
			CFRelease(uid);
		}
	});
	auto size = UInt32(sizeof(uid));
	const auto address = AudioObjectPropertyAddress{
		.mSelector = kAudioDevicePropertyDeviceUID,
		.mScope = kAudioObjectPropertyScopeGlobal,
		.mElement = kAudioObjectPropertyElementMain,
	};
	const auto result = AudioObjectGetPropertyData(
		deviceId,
		&address,
		0,
		nil,
		&size,
		&uid);
	if (result != kAudioHardwareNoError || !uid) {
		return QString();
	}
	return CFStringToQString(uid);
}

[[nodiscard]] AudioDeviceID GetDeviceByUID(const QString &id) {
	const auto utf = id.toUtf8();
	auto uid = CFStringCreateWithCString(
		nullptr,
		utf.data(),
		kCFStringEncodingUTF8);
	if (!uid) {
		return kAudioObjectUnknown;
	}
	const auto guard = gsl::finally([&] {
		CFRelease(uid);
	});
	const auto address = AudioObjectPropertyAddress{
		.mSelector = kAudioHardwarePropertyDeviceForUID,
		.mScope = kAudioObjectPropertyScopeGlobal,
		.mElement = kAudioObjectPropertyElementMain,
	};
	auto deviceId = AudioDeviceID(kAudioObjectUnknown);
	auto deviceSize = UInt32(sizeof(deviceId));

	AudioValueTranslation value;
	value.mInputData = &uid;
	value.mInputDataSize = sizeof(CFStringRef);
	value.mOutputData = &deviceId;
	value.mOutputDataSize = deviceSize;
	auto valueSize = UInt32(sizeof(AudioValueTranslation));

	const auto result = AudioObjectGetPropertyData(
		kAudioObjectSystemObject,
		&address,
		0,
		0,
		&valueSize,
		&value);
	return (result == noErr) ? deviceId : kAudioObjectUnknown;
}

[[nodiscard]] QString GetDeviceName(
		DeviceType type,
		AudioDeviceID deviceId) {
	auto name = (CFStringRef)nullptr;
	const auto guard = gsl::finally([&] {
		if (name) {
			CFRelease(name);
		}
	});
	auto size = UInt32(sizeof(name));
	const auto address = AudioObjectPropertyAddress{
		.mSelector = kAudioDevicePropertyDeviceNameCFString,
		.mScope = (type == DeviceType::Playback
			? kAudioObjectPropertyScopeOutput
			: kAudioObjectPropertyScopeInput),
		.mElement = kAudioObjectPropertyElementMain,
	};
	const auto result = AudioObjectGetPropertyData(
		deviceId,
		&address,
		0,
		nullptr,
		&size,
		&name);
	if (result != kAudioHardwareNoError || !name) {
		return QString();
	}
	return CFStringToQString(name);
}

[[nodiscard]] bool DeviceHasType(
		AudioDeviceID deviceId,
		DeviceType type) {
	auto size = UInt32(0);
	const auto address = AudioObjectPropertyAddress{
		.mSelector = kAudioDevicePropertyStreamConfiguration,
		.mScope = (type == DeviceType::Playback
			? kAudioObjectPropertyScopeOutput
			: kAudioObjectPropertyScopeInput),
		.mElement = kAudioObjectPropertyElementMain,
	};
	auto result = AudioObjectGetPropertyDataSize(
		deviceId,
		&address,
		0,
		0,
		&size);
	if (result != noErr) {
		return false;
	}


	AudioBufferList *list = (AudioBufferList*)malloc(size);
	const auto guard = gsl::finally([&] {
		free(list);
	});
	result = AudioObjectGetPropertyData(
		deviceId,
		&address,
		0,
		nil,
		&size,
		list);
	return (result == noErr) && (list->mNumberBuffers > 0);
}

[[nodiscard]] std::vector<AudioDeviceID> GetAllDeviceIds() {
	auto size = UInt32();
	const auto address = AudioObjectPropertyAddress{
		.mSelector = kAudioHardwarePropertyDevices,
		.mScope = kAudioObjectPropertyScopeGlobal,
		.mElement = kAudioObjectPropertyElementMain,
	};

	auto result = AudioObjectGetPropertyDataSize(
		kAudioObjectSystemObject,
		&address,
		0,
		nil,
		&size);
	if (result != noErr) {
		return {};
	}

	const auto count = size / sizeof(AudioDeviceID);
	auto list = std::vector<AudioDeviceID>(count, kAudioDeviceUnknown);
	result = AudioObjectGetPropertyData(
		kAudioObjectSystemObject,
		&address,
		0,
		nil,
		&size,
		list.data());
	return (result == noErr) ? list : std::vector<AudioDeviceID>();
}

[[nodiscard]] QString NS2QString(NSString *text) {
	return QString::fromUtf8(
		[text cStringUsingEncoding:NSUTF8StringEncoding]);
}

[[nodiscard]] QString CaptureDeviceId(AVCaptureDevice *device) {
	return device ? NS2QString([device uniqueID]) : QString();
}

[[nodiscard]] DeviceInfo CaptureDeviceInfo(AVCaptureDevice *device) {
	const auto id = CaptureDeviceId(device);
	if (id.isEmpty()) {
		return {};
	}
	return {
		.id = id,
		.name = NS2QString([device localizedName]),
		.type = DeviceType::Camera,
	};
}

} // namespace

EnvironmentMac::EnvironmentMac(not_null<EnvironmentDelegate*> delegate)
: _delegate(delegate) {
	DefaultPlaybackDeviceChangedMonitor.registerEnvironment(this);
	DefaultCaptureDeviceChangedMonitor.registerEnvironment(this);
	AudioDeviceListChangedMonitor.registerEnvironment(this);

	if (@available(macOS 14.0, *)) {
		const auto weak = base::make_weak(this);
		id block = [^(BOOL shouldBeMuted){
			crl::on_main([weak, mute = shouldBeMuted ? true : false] {
				if (const auto strong = weak.get()) {
					if (const auto tracker = strong->_captureMuteTracker) {
						strong->_captureMuted = mute;
						strong->_captureMuteNotification = true;
						tracker->captureMuteChanged(mute);
						if (const auto strong = weak.get()) {
							strong->_captureMuteNotification = false;
						}
					}
				}
			});
			return YES;
		} copy];
		_lifetime.add([block] { [block release]; });

		[[AVAudioApplication sharedInstance]
			setInputMuteStateChangeHandler:block
			error:nil];
	}
}

EnvironmentMac::~EnvironmentMac() {
	DefaultPlaybackDeviceChangedMonitor.unregisterEnvironment(this);
	DefaultCaptureDeviceChangedMonitor.unregisterEnvironment(this);
	AudioDeviceListChangedMonitor.unregisterEnvironment(this);
}

void EnvironmentMac::defaultPlaybackDeviceChanged() {
	const auto type = DeviceType::Playback;
	_delegate->defaultChanged(type, DeviceChangeReason::Manual, defaultId(type));
}

void EnvironmentMac::defaultCaptureDeviceChanged() {
	const auto type = DeviceType::Capture;
	_delegate->defaultChanged(type, DeviceChangeReason::Manual, defaultId(type));
}

void EnvironmentMac::audioDeviceListChanged() {
	_delegate->devicesForceRefresh(DeviceType::Playback);
	_delegate->devicesForceRefresh(DeviceType::Capture);
}

QString EnvironmentMac::defaultId(DeviceType type) {
	if (type == DeviceType::Camera) {
		return CaptureDeviceId(
			[AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo]);
	}

	auto deviceId = AudioDeviceID(kAudioDeviceUnknown);
	auto size = UInt32(sizeof(deviceId));
	const auto address = AudioObjectPropertyAddress{
		.mSelector = (type == DeviceType::Playback
			? kAudioHardwarePropertyDefaultOutputDevice
			: kAudioHardwarePropertyDefaultInputDevice),
		.mScope = kAudioObjectPropertyScopeGlobal,
		.mElement = kAudioObjectPropertyElementMain,
	};
	const auto result = AudioObjectGetPropertyData(
		kAudioObjectSystemObject,
		&address,
		0,
		0,
		&size,
		&deviceId);
	return (result == kAudioHardwareNoError) ? GetDeviceUID(deviceId) : QString();
}

DeviceInfo EnvironmentMac::device(DeviceType type, const QString &id) {
	if (type == DeviceType::Camera) {
		NSArray<AVCaptureDevice*> *devices
			= [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
		for (AVCaptureDevice *device in devices) {
			if (CaptureDeviceId(device) == id) {
				return CaptureDeviceInfo(device);
			}
		}
		return {};
	}
	const auto deviceId = GetDeviceByUID(id);
	if (deviceId != kAudioDeviceUnknown) {
		return {
			.id = id,
			.name = GetDeviceName(type, deviceId),
			.type = type,
			.inactive = false,
		};
	}
	const auto list = devices(type);
	return list.empty() ? DeviceInfo() : list.front();
}

std::vector<DeviceInfo> EnvironmentMac::devices(DeviceType type) {
	if (type == DeviceType::Camera) {
		auto result = std::vector<DeviceInfo>();
		const auto add = [&](AVCaptureDevice *device) {
			if (auto info = CaptureDeviceInfo(device)) {
				if (!ranges::contains(result, info.id, &DeviceInfo::id)) {
					result.push_back(std::move(info));
				}
			}
		};
		add([AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo]);
		NSArray<AVCaptureDevice*> *devices
			= [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
		for (AVCaptureDevice *device in devices) {
			add(device);
		}
		return result;
	}
	auto result = std::vector<DeviceInfo>();
	for (const auto deviceId : GetAllDeviceIds()) {
		if (DeviceHasType(deviceId, type)) {
			if (const auto uid = GetDeviceUID(deviceId); !uid.isEmpty()) {
				result.push_back({
					.id = uid,
					.name = GetDeviceName(type, deviceId),
					.type = type,
					.inactive = false,
				});
			}
		}
	}
	return result;
}

bool EnvironmentMac::refreshFullListOnChange(DeviceType type) {
	// We have simple change notifications, no exact change information.
	return true;
}

bool EnvironmentMac::desktopCaptureAllowed() const {
	if (@available(macOS 11.0, *)) {
		// Screen Recording is required on macOS 10.15 an later.
		// Even if user grants access, restart is required.
		static const auto result = CGPreflightScreenCaptureAccess();
		return result;
	} else if (@available(macOS 10.15, *)) {
		const auto stream = CGDisplayStreamCreate(
			CGMainDisplayID(),
			1,
			1,
			kCVPixelFormatType_32BGRA,
			CFDictionaryRef(),
			^(
				CGDisplayStreamFrameStatus status,
				uint64_t display_time,
				IOSurfaceRef frame_surface,
				CGDisplayStreamUpdateRef updateRef) {
			});
		if (!stream) {
			return false;
		}
		CFRelease(stream);
		return true;
	}
	return true;
}

std::optional<QString> EnvironmentMac::uniqueDesktopCaptureSource() const {
	return {};
}

void EnvironmentMac::defaultIdRequested(DeviceType type) {
}

void EnvironmentMac::devicesRequested(DeviceType type) {
}

void EnvironmentMac::setCaptureMuted(bool muted) {
	if (@available(macOS 14.0, *)) {
		if (!_captureMuteNotification) {
			const auto value = muted ? YES : NO;
			[[AVAudioApplication sharedInstance] setInputMuted:value error:nil];
		}
	}
}

void EnvironmentMac::captureMuteSubscribe() {
	if (@available(macOS 14.0, *)) {
		id observer = [[InputMuteObserver alloc] init];
		[[[NSWorkspace sharedWorkspace] notificationCenter]
			addObserver:observer
			selector:@selector(inputMuteStateChange:)
			name:AVAudioApplicationInputMuteStateChangeNotification
			object:nil];

		_admTaskQueueFactory = webrtc::CreateDefaultTaskQueueFactory();
		const auto saveSetDeviceIdCallback = [=](
				Fn<void(DeviceResolvedId)> setDeviceIdCallback) {
			_admSetDeviceIdCallback = std::move(setDeviceIdCallback);
			if (!_admCaptureDeviceId.isDefault()) {
				_admSetDeviceIdCallback(_admCaptureDeviceId);
			}
		};
		_adm = CreateAudioDeviceModule(
			_admTaskQueueFactory.get(),
			saveSetDeviceIdCallback);

		_captureMuteSubscriptionLifetime.add([=] {
			_admSetDeviceIdCallback = nullptr;
			_adm = nullptr;
			_admTaskQueueFactory = nullptr;

			[[[NSWorkspace sharedWorkspace] notificationCenter]
				removeObserver:observer
				name:AVAudioApplicationInputMuteStateChangeNotification
				object:nil];
			[observer release];
		});
	}
}

void EnvironmentMac::captureMuteUnsubscribe() {
	_captureMuteSubscriptionLifetime.destroy();
}

void EnvironmentMac::captureMuteRestartAdm() {
	_adm->StopRecording();
	_adm->SetRecordingDevice(0);
	if (_adm->InitRecording() == 0) {
		_adm->StartRecording();
	}
}

void EnvironmentMac::setCaptureMuteTracker(
		not_null<CaptureMuteTracker*> tracker,
		bool track) {
	if (@available(macOS 14.0, *)) {
		if (track) {
			if (!_captureMuteTracker) {
				captureMuteSubscribe();
			} else if (_captureMuteTracker == tracker) {
				return;
			}
			_captureMuteTrackerLifetime.destroy();
			_captureMuteTracker = tracker;
			_captureMuteTracker->captureMuteDeviceId(
			) | rpl::start_with_next([=](DeviceResolvedId deviceId) {
				_admSetDeviceIdCallback(deviceId);
				captureMuteRestartAdm();
			}, _captureMuteTrackerLifetime);
		} else if (_captureMuteTracker == tracker) {
			_captureMuteTrackerLifetime.destroy();
			_captureMuteTracker = nullptr;
			captureMuteUnsubscribe();
			if (!_captureMuted) {
				_captureMuted = true;
				setCaptureMuted(true);
			}
		}
	}
}

std::unique_ptr<Environment> CreateEnvironment(
		not_null<EnvironmentDelegate*> delegate) {
	return std::make_unique<EnvironmentMac>(delegate);
}

} // namespace Webrtc::Platform
