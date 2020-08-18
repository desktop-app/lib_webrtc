// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/mac/webrtc_media_devices_mac.h"

#import <AVFoundation/AVFoundation.h>

namespace Webrtc {
namespace {

[[nodiscard]] QString NS2QString(NSString *text) {
	return QString::fromUtf8([text cStringUsingEncoding:NSUTF8StringEncoding]);
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

} // namespace Webrtc
