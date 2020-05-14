// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "api/scoped_refptr.h"

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

class Connection final {
public:
	Connection();
	~Connection();

	[[nodiscard]] rpl::producer<IceCandidate> iceCandidateDiscovered() const;
	[[nodiscard]] rpl::producer<bool> connectionStateChanged() const;

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
	void startLocalVideo();

	rpl::event_stream<IceCandidate> _iceCandidateDiscovered;
	rpl::event_stream<bool> _connectionStateChanged;

	std::unique_ptr<rtc::Thread> _networkThread;
	std::unique_ptr<rtc::Thread> _workerThread;
	std::unique_ptr<rtc::Thread> _signalingThread;
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> _nativeFactory;

	std::unique_ptr<PeerConnectionObserver> _observer;
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> _peerConnection;
	std::unique_ptr<webrtc::MediaConstraints> _nativeConstraints;
	bool _hasStartedRtcEventLog;

	rtc::scoped_refptr<webrtc::AudioTrackInterface> _localAudioTrack;

	rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> _nativeVideoSource;
	rtc::scoped_refptr<webrtc::VideoTrackInterface> _localVideoTrack;
	// VideoCameraCapturer *_videoCapturer;

	rtc::scoped_refptr<webrtc::VideoTrackInterface> _remoteVideoTrack;

};

} // namespace Webrtc::details
