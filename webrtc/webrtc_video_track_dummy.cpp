// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_video_track.h"

namespace Webrtc {

VideoTrack::VideoTrack(VideoState state) : _state(state) {
}

VideoTrack::~VideoTrack() {
}

rpl::producer<> VideoTrack::renderNextFrame() const {
	return rpl::never<>();
}

auto VideoTrack::sink()
-> std::shared_ptr<rtc::VideoSinkInterface<webrtc::VideoFrame>> {
	return nullptr;
}

[[nodiscard]] VideoState VideoTrack::state() const {
	return _state.current();
}

[[nodiscard]] rpl::producer<VideoState> VideoTrack::stateValue() const {
	return _state.value();
}

[[nodiscard]] rpl::producer<VideoState> VideoTrack::stateChanges() const {
	return _state.changes();
}

void VideoTrack::setState(VideoState state) {
	_state = state;
}

void VideoTrack::markFrameShown() {
}

QImage VideoTrack::frame(const FrameRequest &request) {
	return QImage();
}

QSize VideoTrack::frameSize() const {
	return QSize();
}

} // namespace Webrtc
