#pragma once
#include <cstdint>
#include <string>
#include <QMetaType>

extern "C" {
#include <libavutil/rational.h>
}

struct StreamInfo {
    int streamIndex = -1;
    int codecId = 0;
    int width = 0;
    int height = 0;
    int sampleRate = 0;
    int channels = 0;
    int sampleFormat = -1;
    int64_t channelLayout = 0;
    AVRational timeBase = {1, 1};
    double durationSec = 0.0;
};

enum class PlayerState {
    Stopped,
    Loading,
    Playing,
    Paused,
    Seeking,
    Error
};

constexpr double AV_SYNC_THRESHOLD_SEC    = 0.01;   // 10ms — acceptable frame-timing window
constexpr double AV_NOSYNC_THRESHOLD_SEC  = 0.1;    // 100ms — drop threshold
constexpr size_t AUDIO_PACKET_QUEUE_MAX_BYTES = 15 * 1024 * 1024;
constexpr size_t VIDEO_PACKET_QUEUE_MAX_BYTES = 30 * 1024 * 1024;
constexpr size_t VIDEO_FRAME_QUEUE_MAX = 3;
constexpr size_t AUDIO_FRAME_QUEUE_MAX = 9;

Q_DECLARE_METATYPE(PlayerState)
