// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/mac/webrtc_media_devices_mac.h"

#include "modules/audio_device/include/audio_device_defines.h"

#import <AVFoundation/AVFoundation.h>

namespace Webrtc {
namespace {

[[nodiscard]] QString NS2QString(NSString *text) {
	return QString::fromUtf8([text cStringUsingEncoding:NSUTF8StringEncoding]);
}

[[nodiscard]] bool IsDefault(const QString &id) {
	return id.isEmpty() || (id == "default");
}

auto AudioOutputDevicePropertyAddress = AudioObjectPropertyAddress{
	.mSelector = kAudioHardwarePropertyDefaultOutputDevice,
	.mScope = kAudioObjectPropertyScopeGlobal,
	.mElement = kAudioObjectPropertyElementMaster,
};

auto AudioDeviceListPropertyAddress = AudioObjectPropertyAddress{
    .mSelector = kAudioHardwarePropertyDevices,
	.mScope = kAudioObjectPropertyScopeGlobal,
    .mElement = kAudioObjectPropertyElementMaster,
};

auto AudioDeviceUIDAddress = AudioObjectPropertyAddress{
	.mSelector = kAudioDevicePropertyDeviceUID,
	.mScope = kAudioObjectPropertyScopeGlobal,
	.mElement = kAudioObjectPropertyElementMaster,
};

OSStatus PropertyChangedCallback(
		AudioObjectID inObjectID,
		UInt32 inNumberAddresses,
		const AudioObjectPropertyAddress *inAddresses,
		void *inClientData) {
	(*reinterpret_cast<Fn<void()>*>(inClientData))();
	return 0;
}

[[nodiscard]] QString GetDeviceUID(AudioDeviceID deviceId) {
	if (deviceId == kAudioDeviceUnknown) {
		return QString();
	}
    CFStringRef uid = NULL;
    UInt32 size = sizeof(uid);
    AudioObjectGetPropertyData(
		deviceId,
		&AudioDeviceUIDAddress,
		0,
		nil,
		&size,
		&uid);

	if (!uid) {
		return QString();
	}

	const auto kLengthLimit = 128;
	char buffer[kLengthLimit + 1] = { 0 };
    const CFIndex kCStringSize = kLengthLimit;
    CFStringGetCString(uid, buffer, kCStringSize, kCFStringEncodingUTF8);
    CFRelease(uid);

	return QString::fromUtf8(buffer);
}

[[nodiscard]] QString ComputeDefaultAudioOutputId() {
	auto deviceId = AudioDeviceID(kAudioDeviceUnknown);
	auto deviceIdSize = UInt32(sizeof(AudioDeviceID));

	AudioObjectGetPropertyData(
		AudioObjectID(kAudioObjectSystemObject),
		&AudioOutputDevicePropertyAddress,
		0,
		nil,
		&deviceIdSize,
		&deviceId);
	return GetDeviceUID(deviceId);
}

[[nodiscard]] QString ResolveAudioInput(const QString &id) {
	return id;
}

[[nodiscard]] QString ResolveAudioOutput(const QString &id) {
	if (IsDefault(id)) {
		const auto desired = ComputeDefaultAudioOutputId();
		if (!desired.isEmpty()) {
			return desired;
		}
	} else {
		const auto utf = id.toUtf8();
		auto uid = CFStringCreateWithCString(NULL, utf.data(), kCFStringEncodingUTF8);
		if (uid) {
			AudioObjectPropertyAddress address = {
				.mSelector = kAudioHardwarePropertyDeviceForUID,
				.mScope = kAudioObjectPropertyScopeGlobal,
				.mElement = kAudioObjectPropertyElementMaster,
			};
			AudioDeviceID deviceId = kAudioObjectUnknown;
			UInt32 deviceSize = sizeof(deviceId);

			AudioValueTranslation value;
			value.mInputData = &uid;
			value.mInputDataSize = sizeof(CFStringRef);
			value.mOutputData = &deviceId;
			value.mOutputDataSize = deviceSize;
			UInt32 valueSize = sizeof(AudioValueTranslation);

			OSStatus result = AudioObjectGetPropertyData(
				kAudioObjectSystemObject,
				&address,
				0,
				0,
				&valueSize,
				&value);
			CFRelease(uid);

			if (result) {
				return id; // Validated.
			}
		}
	}

	auto listSize = UInt32();
	auto listAddress = AudioObjectPropertyAddress{
		.mSelector = kAudioHardwarePropertyDevices,
		.mScope = kAudioObjectPropertyScopeGlobal,
		.mElement = kAudioObjectPropertyElementMaster,
	};

	AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &listAddress, 0, nil, &listSize);

	const auto count = listSize / sizeof(AudioDeviceID);
	auto list = std::vector<AudioDeviceID>(count, kAudioDeviceUnknown);

	AudioObjectGetPropertyData(kAudioObjectSystemObject, &listAddress, 0, nil, &listSize, list.data());

	for (const auto deviceId : list) {
		if (const auto uid = GetDeviceUID(deviceId); !uid.isEmpty()) {
			return uid;
		}
	}
	return id;
}

[[nodiscard]] QString ResolveVideoInput(const QString &id) {
	return id;
}

} // namespace

std::vector<VideoInput> MacGetVideoInputList() {
	auto result = std::vector<VideoInput>();
	const auto add = [&](AVCaptureDevice *device) {
		if (!device) {
			return;
		}
		const auto id = NS2QString([device uniqueID]);
		if (ranges::contains(result, id, &VideoInput::id)) {
			return;
		}
		result.push_back(VideoInput{
			.id = id,
			.name = NS2QString([device localizedName]),
		});
	};
    add([AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo]);

    NSArray<AVCaptureDevice*> *devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
    for (AVCaptureDevice *device in devices) {
		add(device);
	}
	return result;
}

MacMediaDevices::MacMediaDevices(
	QString audioInput,
	QString audioOutput,
	QString videoInput)
: _audioInputId(audioInput)
, _audioOutputId(audioOutput)
, _videoInputId(videoInput) {
	audioInputRefreshed();
	audioOutputRefreshed();
	videoInputRefreshed();
}

MacMediaDevices::~MacMediaDevices() {
	clearAudioOutputCallbacks();
}

void MacMediaDevices::switchToAudioInput(QString id) {
	if (_audioInputId == id) {
		return;
	}
	_audioInputId = id;
	audioInputRefreshed();
}

void MacMediaDevices::switchToAudioOutput(QString id) {
	if (_audioOutputId == id) {
		return;
	}
	_audioOutputId = id;
	audioOutputRefreshed();
}

void MacMediaDevices::switchToVideoInput(QString id) {
	if (_videoInputId == id) {
		return;
	}
	_videoInputId = id;
	videoInputRefreshed();
}

void MacMediaDevices::audioInputRefreshed() {
	_resolvedAudioInputId = ResolveAudioInput(_audioInputId);
}

void MacMediaDevices::audioOutputRefreshed() {
	clearAudioOutputCallbacks();
	const auto refresh = [=] {
		_resolvedAudioOutputId = ResolveAudioOutput(_audioOutputId);
	};
	if (IsDefault(_audioOutputId)) {
		_defaultAudioOutputChanged = refresh;
		AudioObjectAddPropertyListener(
			AudioObjectID(kAudioObjectSystemObject),
			&AudioOutputDevicePropertyAddress,
			PropertyChangedCallback,
			&_defaultAudioOutputChanged);
		_defaultAudioOutputChanged();
	} else {
		_audioOutputDevicesChanged = refresh;
		AudioObjectAddPropertyListener(
			AudioObjectID(kAudioObjectSystemObject),
			&AudioDeviceListPropertyAddress,
			PropertyChangedCallback,
			&_audioOutputDevicesChanged);
	}
	refresh();
}

void MacMediaDevices::clearAudioOutputCallbacks() {
	if (_defaultAudioOutputChanged) {
		AudioObjectRemovePropertyListener(
			AudioObjectID(kAudioObjectSystemObject),
			&AudioOutputDevicePropertyAddress,
			PropertyChangedCallback,
			&_defaultAudioOutputChanged);
		_defaultAudioOutputChanged = nullptr;
	}
	if (_audioOutputDevicesChanged) {
		AudioObjectAddPropertyListener(
			AudioObjectID(kAudioObjectSystemObject),
			&AudioDeviceListPropertyAddress,
			PropertyChangedCallback,
			&_audioOutputDevicesChanged);
		_audioOutputDevicesChanged = nullptr;
	}
}

void MacMediaDevices::videoInputRefreshed() {
	_resolvedVideoInputId = ResolveVideoInput(_videoInputId);
}

} // namespace Webrtc
