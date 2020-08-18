// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace Webrtc {

struct VideoInput {
	QString id;
	QString name;
};

[[nodiscard]] std::vector<VideoInput> GetVideoInputList();

struct AudioInput {
	QString id;
	QString name;
};

[[nodiscard]] std::vector<AudioInput> GetAudioInputList();

struct AudioOutput {
	QString id;
	QString name;
};

[[nodiscard]] std::vector<AudioOutput> GetAudioOutputList();

} // namespace Webrtc
