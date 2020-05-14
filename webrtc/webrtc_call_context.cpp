// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_call_context.h"

#include "webrtc/webrtc_common.h"
#include "webrtc/details/webrtc_connection.h"
#include "base/call_delayed.h"

#include <QtCore/QJsonObject>
#include <QtCore/QJsonDocument>

namespace Webrtc {
namespace {

using namespace details;

constexpr auto kDefaultReceiveTimeout = 20 * crl::time(1000);
constexpr auto kDefaultRingTimeout = 90 * crl::time(1000);
constexpr auto kDefaultConnectTimeout = 30 * crl::time(1000);
constexpr auto kDefaultPacketTimeout = 10 * crl::time(1000);
constexpr auto kRetryAdvertisingTimeout = crl::time(1000);

} // namespace

CallContext::CallContext(const Config &config)
: _receiveTimeout(kDefaultReceiveTimeout)
, _ringTimeout(kDefaultRingTimeout)
, _connectTimeout(kDefaultConnectTimeout)
, _packetTimeout(kDefaultPacketTimeout) {
    init(config);
}

CallContext::~CallContext() = default;

const rpl::variable<CallState> &CallContext::state() const {
    return _state;
}

void CallContext::init(const Config &config) {
	static const auto initialized = Initialize();

	_outgoing = config.outgoing;
    _sendSignalingData = config.sendSignalingData;

    //[RTCAudioSession sharedInstance] .useManualAudio = true;
    //[RTCAudioSession sharedInstance] .isAudioEnabled = true;

	_connection = std::make_unique<Connection>();
	_connection->iceCandidateDiscovered(
	) | rpl::start_with_next([=](const IceCandidate &data) {
        sendCandidate(data);
    }, _lifetime);
    _connection->connectionStateChanged(
    ) | rpl::start_with_next([=](bool connected) {
        _state = connected ? CallState::Connected : CallState::Initializing;
    }, _lifetime);

    if (_outgoing) {
        _connection->getOffer([=](const DescriptionWithType &data) {
            _connection->setLocalDescription(data, [=] {
                tryAdvertising(data);
            });
        });
    }
}

int CallContext::MaxLayer() {
	return 80;
}

QString CallContext::Version() {
	return u"2.8.8"_q;
}

void CallContext::tryAdvertising(const DescriptionWithType &data) {
	if (_receivedRemoteDescription) {
		return;
	}

	sendSdp(data);
	base::call_delayed(kRetryAdvertisingTimeout, this, [=] {
		tryAdvertising(data);
	});
}

void CallContext::stop() {
	_connection->close();
}

void CallContext::sendSdp(const DescriptionWithType &data) {
	auto json = QJsonObject();
	json.insert(u"messageType"_q, u"sessionDescription"_q);
	json.insert(u"sdp"_q, data.sdp);
	json.insert(u"type"_q, data.type);
	_sendSignalingData(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

void CallContext::sendCandidate(const IceCandidate &data) {
	auto json = QJsonObject();
	json.insert(u"messageType"_q, u"iceCandidate"_q);
	json.insert(u"sdp"_q, data.sdp);
	json.insert(u"mLineIndex"_q, data.mLineIndex);
	if (!data.sdpMid.isEmpty()) {
		json.insert(u"sdpMid"_q, data.sdpMid);
	}
	_sendSignalingData(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

void CallContext::receiveSignalingData(const QByteArray &data) {
	const auto parsed = QJsonDocument::fromJson(data);
	if (!parsed.isObject()) {
		return;
	}
	const auto object = parsed.object();
	const auto messageType = object.value(u"messageType"_q).toString();
	if (messageType.isEmpty()) {
		return;
	} else if (messageType == u"sessionDescription"_q) {
		receiveSessionDescription(object);
	} else if (messageType == u"iceCandidate"_q) {
		receiveIceCandidate(object);
	}
}

void CallContext::receiveSessionDescription(const QJsonObject &object) {
	if (_receivedRemoteDescription) {
		return;
	}

	const auto sdpObject = object.value(u"sdp"_q);
	const auto typeObject = object.value(u"type"_q);
	if (!sdpObject.isString() || !typeObject.isString()) {
		return;
	}
	const auto data = DescriptionWithType{
		.sdp = sdpObject.toString(),
		.type = typeObject.toString(),
	};
	_receivedRemoteDescription = true;
	_connection->setRemoteDescription(data, [=] {
		if (!_outgoing) {
			_connection->getAnswer([=](const DescriptionWithType &data) {
				_connection->setLocalDescription(data, [=] {
					sendSdp(data);
				});
			});
		}
	});
}

void CallContext::receiveIceCandidate(const QJsonObject &object) {
	const auto sdpObject = object.value(u"sdp"_q);
	const auto mLineIndexObject = object.value(u"mLineIndex"_q);
	const auto sdpMidObject = object.value(u"sdpMid"_q);
	if (!sdpObject.isString() || !mLineIndexObject.isDouble()) {
		return;
	}
	const auto data = IceCandidate{
		.sdp = sdpObject.toString(),
		.sdpMid = sdpMidObject.toString(),
		.mLineIndex = mLineIndexObject.toInt(),
	};
	_connection->addIceCandidate(data);
}

void CallContext::setIsMuted(bool muted) {
	_connection->setIsMuted(muted);
}

} // namespace Webrtc
