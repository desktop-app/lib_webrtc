// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/weak_ptr.h"

#include <crl/crl_object_on_queue.h>

class QJsonObject;
class QImage;

namespace Webrtc {
namespace details {
struct DescriptionWithType;
struct IceCandidate;
class Connection;
} // namespace details

struct CallConnectionDescription {
	QString ip;
	QString ipv6;
	QByteArray peerTag;
	int64 connectionId = 0;
	int32 port = 0;
};

enum class CallState {
	Initializing,
	Connected,
	Failed,
	Reconnecting,
};

struct ProxyServer {
	QString host;
	QString username;
	QString password;
	int32 port = 0;
};

class CallContext final : public base::has_weak_ptr {
public:
	struct Config {
		ProxyServer proxy;
		bool dataSaving = false;
		QByteArray key;
		bool outgoing = false;
		CallConnectionDescription primary;
		std::vector<CallConnectionDescription> alternatives;
		int maxLayer = 0;
		bool allowP2P = false;
		Fn<void(QByteArray)> sendSignalingData;
		Fn<void(QImage)> displayNextFrame;
	};
	explicit CallContext(const Config &config);
	~CallContext();

	const rpl::variable<CallState> &state() const;

	void setIsMuted(bool muted);
	[[nodiscard]] QString getDebugInfo();
	void stop();

	bool receiveSignalingData(const QByteArray &json);

	[[nodiscard]] static int MaxLayer();
	[[nodiscard]] static QString Version();

private:
	void init(const Config &config);

	void tryAdvertising(const details::DescriptionWithType &data);
	void sendSdp(const details::DescriptionWithType &data);
	void sendCandidate(const details::IceCandidate &data);
	void receiveSessionDescription(const QJsonObject &object);
	void receiveIceCandidate(const QJsonObject &object);

	crl::object_on_queue<details::Connection> _connection;

	bool _outgoing = false;
	Fn<void(QByteArray)> _sendSignalingData;
	Fn<void(QImage)> _displayNextFrame;
	rpl::variable<CallState> _state = CallState::Initializing;
	bool _receivedRemoteDescription = false;
	rpl::lifetime _lifetime;

};

} // namespace Webrtc
