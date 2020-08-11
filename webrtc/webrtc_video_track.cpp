// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_video_track.h"

#include "ffmpeg/ffmpeg_utility.h"

#include <api/video/video_sink_interface.h>
#include <api/video/video_frame.h>
#include <QtGui/QImage>
#include <QtGui/QPainter>

namespace Webrtc {
namespace {

constexpr auto kDropFramesWhileInactive = 5 * crl::time(1000);

[[nodiscard]] bool GoodForRequest(
		const QImage &image,
		int rotation,
		const FrameRequest &request) {
	if (request.resize.isEmpty()) {
		return true;
	} else if (rotation != 0) {
		return false;
	//} else if ((request.radius != ImageRoundRadius::None)
	//	&& ((request.corners & RectPart::AllCorners) != 0)) {
	//	return false;
	}
	return (request.resize == request.outer)
		&& (request.resize == image.size());
}

void PaintFrameOuter(QPainter &p, const QRect &inner, QSize outer) {
	const auto left = inner.x();
	const auto right = outer.width() - inner.width() - left;
	const auto top = inner.y();
	const auto bottom = outer.height() - inner.height() - top;
	if (left > 0) {
		p.fillRect(0, 0, left, outer.height(), Qt::black);
	}
	if (right > 0) {
		p.fillRect(
			outer.width() - right,
			0,
			right,
			outer.height(),
			Qt::black);
	}
	if (top > 0) {
		p.fillRect(left, 0, inner.width(), top, Qt::black);
	}
	if (bottom > 0) {
		p.fillRect(
			left,
			outer.height() - bottom,
			inner.width(),
			bottom,
			Qt::black);
	}
}

void PaintFrameInner(
		QPainter &p,
		QRect to,
		const QImage &original,
		bool alpha,
		int rotation) {
	const auto rotated = [](QRect rect, int rotation) {
		switch (rotation) {
		case 0: return rect;
		case 90: return QRect(
			rect.y(),
			-rect.x() - rect.width(),
			rect.height(),
			rect.width());
		case 180: return QRect(
			-rect.x() - rect.width(),
			-rect.y() - rect.height(),
			rect.width(),
			rect.height());
		case 270: return QRect(
			-rect.y() - rect.height(),
			rect.x(),
			rect.height(),
			rect.width());
		}
		Unexpected("Rotation in PaintFrameInner.");
	};

	const auto hints = {
		QPainter::Antialiasing,
		QPainter::SmoothPixmapTransform,
		QPainter::TextAntialiasing,
		QPainter::HighQualityAntialiasing
	};
	for (const auto hint : hints) {
		p.setRenderHint(hint);
	}
	if (rotation) {
		p.rotate(rotation);
	}
	const auto rect = rotated(to, rotation);
	if (alpha) {
		p.fillRect(rect, Qt::white);
	}
	p.drawImage(rect, original);
}

void PaintFrameContent(
		QPainter &p,
		const QImage &original,
		bool alpha,
		int rotation,
		const FrameRequest &request) {
	const auto full = request.outer.isEmpty()
		? original.size()
		: request.outer;
	const auto size = request.resize.isEmpty()
		? original.size()
		: request.resize;
	const auto to = QRect(
		(full.width() - size.width()) / 2,
		(full.height() - size.height()) / 2,
		size.width(),
		size.height());
	PaintFrameOuter(p, to, full);
	PaintFrameInner(p, to, original, alpha, rotation);
}

void ApplyFrameRounding(QImage &storage, const FrameRequest &request) {
	//if (!(request.corners & RectPart::AllCorners)
	//	|| (request.radius == ImageRoundRadius::None)) {
	//	return;
	//}
	//Images::prepareRound(storage, request.radius, request.corners);
}

QImage PrepareByRequest(
		const QImage &original,
		bool alpha,
		int rotation,
		const FrameRequest &request,
		QImage storage) {
	Expects(!request.outer.isEmpty() || alpha);

	const auto outer = request.outer.isEmpty()
		? original.size()
		: request.outer;
	if (!FFmpeg::GoodStorageForFrame(storage, outer)) {
		storage = FFmpeg::CreateFrameStorage(outer);
	}

	QPainter p(&storage);
	PaintFrameContent(p, original, alpha, rotation, request);
	p.end();

	ApplyFrameRounding(storage, request);
	return storage;
}

} // namespace

class VideoTrack::Sink final
	: public rtc::VideoSinkInterface<webrtc::VideoFrame>
	, public std::enable_shared_from_this<Sink> {
public:
	using PrepareFrame = not_null<Frame*>;
	using PrepareState = bool;

	[[nodiscard]] bool firstPresentHappened() const;

	// Called from the main thread.
	void markFrameShown();
	[[nodiscard]] not_null<Frame*> frameForPaint();
	[[nodiscard]] rpl::producer<> renderNextFrameOnMain() const;
	void destroyFrameForPaint();

	void OnFrame(const webrtc::VideoFrame &nativeVideoFrame) override;

private:
	struct FrameForDecode {
		not_null<Frame*> frame;
		int counter = 0;
	};
	[[nodiscard]] FrameForDecode nextFrameForDecode();
	void presentNextFrame(const FrameForDecode &frame);
	[[nodiscard]] not_null<Frame*> getFrame(int index);
	[[nodiscard]] not_null<const Frame*> getFrame(int index) const;
	[[nodiscard]] int counter() const;

	bool decodeFrame(
		const webrtc::VideoFrame &nativeVideoFrame,
		not_null<Frame*> frame);
	void notifyFrameDecoded();

	FFmpeg::SwscalePointer _decodeContext;

	std::atomic<int> _counter = 0;

	static constexpr auto kFramesCount = 3;
	std::array<Frame, kFramesCount> _frames;

	rpl::event_stream<> _renderNextFrameOnMain;

};

void VideoTrack::Sink::OnFrame(const webrtc::VideoFrame &nativeVideoFrame) {
	const auto decode = nextFrameForDecode();
	if (decodeFrame(nativeVideoFrame, decode.frame)) {
		PrepareFrameByRequests(decode.frame, nativeVideoFrame.rotation());
		presentNextFrame(decode);
	}
}

auto VideoTrack::Sink::nextFrameForDecode() -> FrameForDecode {
	const auto current = counter();
	const auto index = ((current + 3) / 2) % kFramesCount;
	const auto frame = getFrame(index);
	const auto next = getFrame((index + 1) % kFramesCount);
	return { frame, current };
}

void VideoTrack::Sink::presentNextFrame(const FrameForDecode &frame) {
	// Release this frame to the main thread for rendering.
	const auto present = [&](int counter) {
		_counter.store(
			(counter + 1) % (2 * kFramesCount),
			std::memory_order_release);
		notifyFrameDecoded();
	};
	switch (frame.counter) {
	case 0: present(0);
	case 1: return;
	case 2: present(2);
	case 3: return;
	case 4: present(4);
	case 5: return;
	//case 6: present(6);
	//case 7: return;
	}
	Unexpected("Counter value in VideoTrack::Sink::presentNextFrame.");
}

bool VideoTrack::Sink::decodeFrame(
		const webrtc::VideoFrame &nativeVideoFrame,
		not_null<Frame*> frame) {
	const auto native = nativeVideoFrame.video_frame_buffer()->ToI420();
	const auto size = QSize{ native->width(), native->height() };
	if (size.isEmpty()) {
		return false;
	}
	if (!FFmpeg::GoodStorageForFrame(frame->original, size)) {
		frame->original = FFmpeg::CreateFrameStorage(size);
	}
	_decodeContext = FFmpeg::MakeSwscalePointer(
		size,
		AV_PIX_FMT_YUV420P,
		size,
		AV_PIX_FMT_BGRA,
		&_decodeContext);
	Assert(_decodeContext != nullptr);

	// AV_NUM_DATA_POINTERS defined in AVFrame struct
	const uint8_t *src[AV_NUM_DATA_POINTERS] = {
		native->DataY(),
		native->DataU(),
		native->DataV(),
		nullptr
	};
	int srcLineSize[AV_NUM_DATA_POINTERS] = {
		native->StrideY(),
		native->StrideU(),
		native->StrideV(),
		0
	};
	uint8_t *dst[AV_NUM_DATA_POINTERS] = { frame->original.bits(), nullptr };
	int dstLineSize[AV_NUM_DATA_POINTERS] = { frame->original.bytesPerLine(), 0 };

	const auto lines = sws_scale(
		_decodeContext.get(),
		src,
		srcLineSize,
		0,
		frame->original.height(),
		dst,
		dstLineSize);

	Ensures(lines == frame->original.height());
	return true;
}

void VideoTrack::Sink::notifyFrameDecoded() {
	crl::on_main([weak = weak_from_this()] {
		if (const auto strong = weak.lock()) {
			strong->_renderNextFrameOnMain.fire({});
		}
	});
}

int VideoTrack::Sink::counter() const {
	return _counter.load(std::memory_order_acquire);
}

not_null<VideoTrack::Frame*> VideoTrack::Sink::getFrame(int index) {
	Expects(index >= 0 && index < kFramesCount);

	return &_frames[index];
}

not_null<const VideoTrack::Frame*> VideoTrack::Sink::getFrame(
	int index) const {
	Expects(index >= 0 && index < kFramesCount);

	return &_frames[index];
}

// Sometimes main thread subscribes to check frame requests before
// the first frame is ready and presented and sometimes after.
bool VideoTrack::Sink::firstPresentHappened() const {
	switch (counter()) {
	case 0: return false;
	case 1: return true;
	}
	Unexpected("Counter value in VideoTrack::Sink::firstPresentHappened.");
}

 void VideoTrack::Sink::markFrameShown() {
	const auto jump = [&](int counter) {
		const auto next = (counter + 1) % (2 * kFramesCount);
		const auto index = next / 2;
		const auto frame = getFrame(index);
		frame->displayed = true;
		_counter.store(
			next,
			std::memory_order_release);
	};

	switch (counter()) {
	case 0: return;
	case 1: return jump(1);
	case 2: return;
	case 3: return jump(3);
	case 4: return;
	case 5: return jump(5);
	//case 6: return;
	//case 7: return jump(7);
	}
	Unexpected("Counter value in VideoTrack::Sink::markFrameShown.");
}

not_null<VideoTrack::Frame*> VideoTrack::Sink::frameForPaint() {
	return getFrame(counter() / 2);
}

void VideoTrack::Sink::destroyFrameForPaint() {
	const auto frame = getFrame(counter() / 2);
	if (!frame->original.isNull()) {
		frame->original = frame->prepared = QImage();
	}
}

rpl::producer<> VideoTrack::Sink::renderNextFrameOnMain() const {
	return _renderNextFrameOnMain.events();
}

VideoTrack::VideoTrack(VideoState state) : _state(state) {
	_sink = std::make_shared<Sink>();
}

VideoTrack::~VideoTrack() {
}

rpl::producer<> VideoTrack::renderNextFrame() const {
	return rpl::merge(
		_sink->renderNextFrameOnMain(),
		_state.changes() | rpl::to_empty);
}

auto VideoTrack::sink()
-> std::shared_ptr<rtc::VideoSinkInterface<webrtc::VideoFrame>> {
	return _sink;
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
	if (state == VideoState::Active) {
		_disabledFrom = 0;
	} else {
		_disabledFrom = crl::now();
	}
	_state = state;
	if (state != VideoState::Active) {
		// save last frame?..
		_sink->destroyFrameForPaint();
	}
}

void VideoTrack::markFrameShown() {
	_sink->markFrameShown();
}

QImage VideoTrack::frame(const FrameRequest &request) {
	if (_disabledFrom > 0
		&& (_disabledFrom + kDropFramesWhileInactive > crl::now())) {
		_sink->destroyFrameForPaint();
		return QImage();
	}
	const auto frame = _sink->frameForPaint();
	const auto preparedFor = frame->request;
	const auto changed = !preparedFor.goodFor(request);
	const auto useRequest = changed ? request : preparedFor;
	if (changed) {
		//_wrapped.with([=](Implementation &unwrapped) {
		//	unwrapped.updateFrameRequest(instance, useRequest);
		//});
	}
	if (!frame->alpha
		&& GoodForRequest(frame->original, frame->rotation, useRequest)) {
		return frame->original;
	} else if (changed || frame->prepared.isNull()) {
		if (changed) {
			frame->request = useRequest;
		}
		frame->prepared = PrepareByRequest(
			frame->original,
			frame->alpha,
			frame->rotation,
			useRequest,
			std::move(frame->prepared));
	}
	return frame->prepared;
}

QSize VideoTrack::frameSize() const {
	return _sink->frameForPaint()->original.size();
}

void VideoTrack::PrepareFrameByRequests(
		not_null<Frame*> frame,
		int rotation) {
	Expects(!frame->original.isNull());

	if (frame->alpha
		|| !GoodForRequest(frame->original, rotation, frame->request)) {
		frame->prepared = PrepareByRequest(
			frame->original,
			frame->alpha,
			rotation,
			frame->request,
			std::move(frame->prepared));
	}
}

} // namespace Webrtc
