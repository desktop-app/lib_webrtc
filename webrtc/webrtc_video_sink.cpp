// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_video_sink.h"

#include "ffmpeg/ffmpeg_utility.h"

#include <api/video/video_sink_interface.h>
#include <api/video/video_frame.h>
#include <QtGui/QImage>
#include <QtGui/QPainter>

namespace webrtc {
namespace {

class VideoRendererAdapter : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
	explicit VideoRendererAdapter(Fn<void(QImage)> frame)
	: _sendFrame(std::move(frame)) {
	}

	void OnFrame(const webrtc::VideoFrame &nativeVideoFrame) override {
		if (decodeFrame(nativeVideoFrame)) {
			renderDecodedFrame(nativeVideoFrame.rotation());
			_sendFrame(_cache);
		}
	}

private:
	bool decodeFrame(const webrtc::VideoFrame &nativeVideoFrame);
	void renderDecodedFrame(int rotation);
	void renderRotatedFrame(int rotation);

	QImage _original, _cache;
	FFmpeg::SwscalePointer _decodeContext;
	Fn<void(QImage)> _sendFrame;

};

bool VideoRendererAdapter::decodeFrame(const webrtc::VideoFrame &nativeVideoFrame) {
	const auto frame = nativeVideoFrame.video_frame_buffer()->ToI420();
	const auto size = QSize{ frame->width(), frame->height() };
	if (size.isEmpty()) {
		return false;
	}
	if (!FFmpeg::GoodStorageForFrame(_original, size)) {
		_original = FFmpeg::CreateFrameStorage(size);
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
		frame->DataY(),
		frame->DataU(),
		frame->DataV(),
		nullptr
	};
	int srcLineSize[AV_NUM_DATA_POINTERS] = {
		frame->StrideY(),
		frame->StrideU(),
		frame->StrideV(),
		0
	};
	uint8_t *dst[AV_NUM_DATA_POINTERS] = { _original.bits(), nullptr };
	int dstLineSize[AV_NUM_DATA_POINTERS] = { _original.bytesPerLine(), 0 };

	const auto lines = sws_scale(
		_decodeContext.get(),
		src,
		srcLineSize,
		0,
		_original.height(),
		dst,
		dstLineSize);

	Ensures(lines == _original.height());
	return true;
}

void VideoRendererAdapter::renderDecodedFrame(int rotation) {
	if (rotation) {
		renderRotatedFrame(rotation);
	} else {
		_cache = _original;
	}
}

void VideoRendererAdapter::renderRotatedFrame(int rotation) {
	const auto size = _original.size();
	const auto cacheSize = FFmpeg::RotationSwapWidthHeight(rotation)
		? size.transposed()
		: size;
	_cache = FFmpeg::CreateFrameStorage(cacheSize);
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
	QPainter p(&_cache);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.setRenderHint(QPainter::HighQualityAntialiasing);
	if (rotation) {
		p.rotate(rotation);
	}
	const auto rect = rotated(_cache.rect(), rotation);
	p.drawImage(rect, _original);
}

} // namespace


auto CreateVideoSink(Fn<void(QImage)> callback)
-> std::shared_ptr<rtc::VideoSinkInterface<webrtc::VideoFrame>> {
	return std::make_shared<VideoRendererAdapter>(std::move(callback));
}

} // namespace webrtc
