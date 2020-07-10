// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

class QImage;

namespace rtc {
template <typename VideoFrameT>
class VideoSinkInterface;
}

namespace webrtc {
class VideoFrame;
}

namespace webrtc {

[[nodiscard]] auto CreateVideoSink(Fn<void(QImage)> callback)
-> std::shared_ptr<rtc::VideoSinkInterface<webrtc::VideoFrame>>;

} // namespace webrtc
