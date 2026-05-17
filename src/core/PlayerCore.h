#pragma once
#include <QObject>
#include <QTimer>
#include <atomic>
#include "Types.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include "Clock.h"
#include "Demuxer.h"
#include "AudioDecoder.h"
#include "VideoDecoder.h"

#include "AudioOutput.h"

class VideoRenderer;

class PlayerCore : public QObject {
    Q_OBJECT
public:
    explicit PlayerCore(QObject* parent = nullptr);
    ~PlayerCore();

    bool openFile(const QString& filePath);
    void close();
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void seek(double seconds);
    void seekRelative(double deltaSeconds);
    void setVolume(double volume);
    void setMuted(bool muted);
    void setPlaybackRate(double rate);

    PlayerState state() const { return state_.load(); }
    double duration() const { return duration_; }
    bool hasAudio() const { return hasAudio_; }
    bool hasVideo() const { return hasVideo_; }

    FrameQueue& videoFrameQueue() { return videoFrameQueue_; }
    Clock& audioClock() { return audioOutput_ ? audioOutput_->audioClock() : fallbackClock_; }

    int videoWidth() const { return videoWidth_; }
    int videoHeight() const { return videoHeight_; }

signals:
    void stateChanged(PlayerState newState);
    void timeChanged(double currentSec, double totalSec);
    void videoInfoReady(int width, int height, double frameRate);
    void errorOccurred(const QString& message);
    void fileLoaded();
    void playbackFinished();
    void seekStarted(double targetSec);

private slots:
    void updatePosition();

private:
    void setState(PlayerState s);
    void startPlayback();
    void stopAllThreads();
    void cleanupResources();
    void onDemuxerFinished();

    // Queues — must be declared before threads that reference them
    PacketQueue audioPacketQueue_{AUDIO_PACKET_QUEUE_MAX_BYTES};
    PacketQueue videoPacketQueue_{VIDEO_PACKET_QUEUE_MAX_BYTES};
    FrameQueue audioFrameQueue_{AUDIO_FRAME_QUEUE_MAX};
    FrameQueue videoFrameQueue_{VIDEO_FRAME_QUEUE_MAX};

    // Process threads
    Demuxer demuxer_{audioPacketQueue_, videoPacketQueue_};
    AudioDecoder audioDecoder_{audioPacketQueue_, audioFrameQueue_};
    VideoDecoder videoDecoder_{videoPacketQueue_, videoFrameQueue_};

    // Output backends
    AudioOutput* audioOutput_ = nullptr;
    VideoRenderer* videoRenderer_ = nullptr;

    // Fallback clock when no audio
    Clock fallbackClock_;

    // State
    std::atomic<PlayerState> state_{PlayerState::Stopped};
    bool wasPausedBeforeSeek_ = false;
    double duration_ = 0.0;
    double volume_ = 1.0;
    double playbackRate_ = 1.0;
    bool muted_ = false;

    bool hasAudio_ = false;
    bool hasVideo_ = false;
    bool demuxerFinished_ = false;

    int videoWidth_ = 0;
    int videoHeight_ = 0;

    QTimer* positionTimer_ = nullptr;
};
