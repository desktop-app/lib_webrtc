// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ffmpeg/ffmpeg_utility.h"

#include <rpl/variable.h>
#include <QtCore/QSize>
#include <QtGui/QImage>

namespace rtc {
template <typename VideoFrameT>
class VideoSinkInterface;
} // namespace rtc

namespace webrtc {
class VideoFrame;
} // namespace webrtc

namespace webrtc {

struct FrameRequest {
	QSize resize;
	QSize outer;
	//ImageRoundRadius radius = ImageRoundRadius();
	//RectParts corners = RectPart::AllCorners;
	bool strict = true;

	static FrameRequest NonStrict() {
		auto result = FrameRequest();
		result.strict = false;
		return result;
	}

	[[nodiscard]] bool empty() const {
		return resize.isEmpty();
	}

	[[nodiscard]] bool operator==(const FrameRequest &other) const {
		return (resize == other.resize)
			&& (outer == other.outer)/*
			&& (radius == other.radius)
			&& (corners == other.corners)*/;
	}
	[[nodiscard]] bool operator!=(const FrameRequest &other) const {
		return !(*this == other);
	}

	[[nodiscard]] bool goodFor(const FrameRequest &other) const {
		return (*this == other) || (strict && !other.strict);
	}
};

enum class VideoState {
	Inactive,
	Paused,
	Active,
};

class VideoTrack final {
public:
	// Called from the main thread.
	explicit VideoTrack(VideoState state);
	~VideoTrack();

	void markFrameShown();
	[[nodiscard]] QImage frame(const FrameRequest &request);
	[[nodiscard]] QSize frameSize() const;
	[[nodiscard]] rpl::producer<> renderNextFrame() const;
	[[nodiscard]] auto sink()
		-> std::shared_ptr<rtc::VideoSinkInterface<webrtc::VideoFrame>>;

	[[nodiscard]] VideoState state() const;
	[[nodiscard]] rpl::producer<VideoState> stateValue() const;
	[[nodiscard]] rpl::producer<VideoState> stateChanges() const;
	void setState(VideoState state);

private:
	class Sink;

	struct Frame {
		FFmpeg::FramePointer decoded = FFmpeg::MakeFramePointer();
		QImage original;
		QImage prepared;
		FrameRequest request = FrameRequest::NonStrict();

		int rotation = 0;
		bool displayed = false;
		bool alpha = false;
	};

	static void PrepareFrameByRequests(not_null<Frame*> frame, int rotation);

	std::shared_ptr<Sink> _sink;
	rpl::variable<VideoState> _state;
	crl::time _disabledFrom = 0;

};

} // namespace webrtc
