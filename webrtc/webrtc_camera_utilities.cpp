// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_camera_utilities.h"

#include "modules/video_capture/video_capture_factory.h"
#include "modules/audio_device/include/audio_device_factory.h"

namespace Webrtc {

std::vector<CameraInfo> GetCamerasList() {
	const auto info = std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo>(
		webrtc::VideoCaptureFactory::CreateDeviceInfo());
	auto result = std::vector<CameraInfo>();
	if (!info) {
		return result;
	}
	const auto count = info->NumberOfDevices();
	for (auto i = uint32_t(); i != count; ++i) {
		constexpr auto kLengthLimit = 4096;
		auto id = std::string(kLengthLimit, char(0));
		auto name = std::string(kLengthLimit, char(0));
		info->GetDeviceName(
			i,
			name.data(),
			name.size(),
			id.data(),
			id.size());
		const auto idEnd = std::find(id.begin(), id.end(), char(0));
		if (idEnd != id.end()) {
			id.erase(idEnd, id.end());
		}
		const auto nameEnd = std::find(name.begin(), name.end(), char(0));
		if (nameEnd != name.end()) {
			name.erase(nameEnd, name.end());
		}
		result.push_back({
			.id = QString::fromStdString(id),
			.name = QString::fromStdString(name)
		});
	}
	return result;
}

} // namespace Webrtc
