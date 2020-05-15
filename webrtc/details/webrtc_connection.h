// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "api/scoped_refptr.h"

#include <crl/crl_object_on_queue.h>

class QImage;

namespace rtc {
class Thread;
} // namespace rtc

namespace webrtc {
class MediaConstraints;
class VideoTrackInterface;
class VideoTrackSourceInterface;
class AudioTrackInterface;
class PeerConnectionInterface;
class PeerConnectionFactoryInterface;
} // namespace webrtc

namespace Webrtc::details {

struct IceCandidate {
	QString sdp;
	QString sdpMid;
	int mLineIndex = 0;
};

struct DescriptionWithType {
	QString sdp;
	QString type;
};

class PeerConnectionObserver;
class VideoRendererAdapter;
class CapturerTrackSource;

class Connection final {
public:
	explicit Connection(crl::weak_on_queue<Connection> weak);
	~Connection();

	[[nodiscard]] rpl::producer<IceCandidate> iceCandidateDiscovered() const;
	[[nodiscard]] rpl::producer<bool> connectionStateChanged() const;
	[[noriscard]] rpl::producer<QImage> frameReceived() const;

	void close();

	void setIsMuted(bool muted);

	void getOffer(Fn<void(DescriptionWithType)> done);
	void getAnswer(Fn<void(DescriptionWithType)> done);
	void setLocalDescription(
		const DescriptionWithType &data,
		Fn<void()> done);
	void setRemoteDescription(
		const DescriptionWithType &data,
		Fn<void()> done);
	void addIceCandidate(const IceCandidate &data);

private:
	void init();
	void startRemoteVideo();

	crl::weak_on_queue<Connection> _weak;
	rpl::event_stream<IceCandidate> _iceCandidateDiscovered;
	rpl::event_stream<bool> _connectionStateChanged;
	rpl::event_stream<QImage> _frames;

	std::unique_ptr<VideoRendererAdapter> _sink;

	std::unique_ptr<rtc::Thread> _networkThread;
	std::unique_ptr<rtc::Thread> _workerThread;
	std::unique_ptr<rtc::Thread> _signalingThread;
	std::unique_ptr<PeerConnectionObserver> _observer;
	std::unique_ptr<webrtc::MediaConstraints> _nativeConstraints;

	// The order is important. CapturerTrackSource should be destroyed on
	// the Connection's thread, so this pointer should be here, before
	// the VideoTrackInterface that holds reference to it.
	rtc::scoped_refptr<CapturerTrackSource> _videoTrackSource;
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> _nativeFactory;
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> _peerConnection;
	rtc::scoped_refptr<webrtc::AudioTrackInterface> _localAudioTrack;
	rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> _nativeVideoSource;
	rtc::scoped_refptr<webrtc::VideoTrackInterface> _localVideoTrack;
	rtc::scoped_refptr<webrtc::VideoTrackInterface> _remoteVideoTrack;

};

} // namespace Webrtc::details
