// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "webrtc/webrtc_device_common.h"

#include <crl/crl_object_on_thread.h>

namespace Webrtc {

class AudioInputTester {
public:
	explicit AudioInputTester(rpl::producer<DeviceResolvedId> deviceId);
	~AudioInputTester();

	[[nodiscard]] float getAndResetLevel();

private:
	class Impl;

	std::shared_ptr<std::atomic<int>> _maxSample;
	crl::object_on_thread<Impl> _impl;
	rpl::lifetime _lifetime;

};

} // namespace Webrtc
