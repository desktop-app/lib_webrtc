// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_common.h"

#include "rtc_base/ssl_adapter.h"

namespace Webrtc {

bool Initialize() {
	return rtc::InitializeSSL();
}

} // namespace Webrtc
