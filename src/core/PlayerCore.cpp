#include "PlayerCore.h"
#include "AudioOutput.h"
#include "VideoRenderer.h"

extern "C" {
#include <libavformat/avformat.h>
}

PlayerCore::PlayerCore(QObject* parent)
    : QObject(parent)
    , audioOutput_(new AudioOutput(this))
{
    demuxer_.setOnFinished([this] { onDemuxerFinished(); });
    demuxer_.setOnError([this](const std::string& msg) {
        QString errorMsg = QString::fromStdString(msg);
        QMetaObject::invokeMethod(this, [this, errorMsg] {
            emit errorOccurred(errorMsg);
            setState(PlayerState::Error);
        }, Qt::QueuedConnection);
    });

    positionTimer_ = new QTimer(this);
    connect(positionTimer_, &QTimer::timeout, this, &PlayerCore::updatePosition);
    positionTimer_->setInterval(100);
}

PlayerCore::~PlayerCore()
{
    close();
}

bool PlayerCore::openFile(const QString& filePath)
{
    close();

    QByteArray utf8Path = filePath.toUtf8();
    if (!demuxer_.open(std::string(utf8Path.constData(), utf8Path.size()))) {
        emit errorOccurred("Failed to open file: " + filePath);
        return false;
    }

    setState(PlayerState::Loading);

    // Open audio decoder
    hasAudio_ = demuxer_.hasAudio();
    if (hasAudio_) {
        if (!audioDecoder_.open(demuxer_.audioCodecParams(), demuxer_.audioStreamInfo().timeBase)) {
            emit errorOccurred("Failed to open audio decoder");
            hasAudio_ = false;
        } else if (!audioOutput_->init(
                       audioDecoder_.sampleRate(),
                       audioDecoder_.channels(),
                       audioDecoder_.sampleFormat(),
                       audioFrameQueue_)) {
            emit errorOccurred("Failed to initialize audio output");
            hasAudio_ = false;
        }
    }
    hasVideo_ = demuxer_.hasVideo();
    if (hasVideo_) {
        if (!videoDecoder_.open(demuxer_.videoCodecParams(), demuxer_.videoStreamInfo().timeBase)) {
            emit errorOccurred("Failed to open video decoder");
            hasVideo_ = false;
        } else {
            videoWidth_ = videoDecoder_.width();
            videoHeight_ = videoDecoder_.height();
        }
    }

    if (!hasAudio_ && !hasVideo_) {
        emit errorOccurred("No playable audio or video stream found");
        setState(PlayerState::Error);
        return false;
    }

    duration_ = 0.0;
    if (demuxer_.audioStreamInfo().durationSec > 0.0)
        duration_ = demuxer_.audioStreamInfo().durationSec;
    if (demuxer_.videoStreamInfo().durationSec > duration_)
        duration_ = demuxer_.videoStreamInfo().durationSec;
    // Fallback: try format context duration
    if (duration_ <= 0.0) {
        AVFormatContext* fc = demuxer_.formatContext();
        if (fc && fc->duration != AV_NOPTS_VALUE)
            duration_ = fc->duration / (double)AV_TIME_BASE;
    }

    emit fileLoaded();
    emit videoInfoReady(videoWidth_, videoHeight_, 0);

    startPlayback();
    return true;
}

void PlayerCore::close()
{
    stop();
    cleanupResources();
}

void PlayerCore::play()
{
    PlayerState s = state_.load();
    if (s == PlayerState::Paused) {
        if (hasAudio_)
            audioOutput_->resume();
        else if (hasVideo_)
            audioOutput_->audioClock().resume();
        setState(PlayerState::Playing);
        positionTimer_->start();
    } else if (s == PlayerState::Stopped) {
        // Re-open needed
    }
}

void PlayerCore::pause()
{
    if (state_.load() == PlayerState::Playing) {
        if (hasAudio_)
            audioOutput_->pause();
        else if (hasVideo_)
            audioOutput_->audioClock().pause();
        setState(PlayerState::Paused);
        positionTimer_->stop();
    }
}

void PlayerCore::togglePlayPause()
{
    PlayerState s = state_.load();
    if (s == PlayerState::Playing)
        pause();
    else if (s == PlayerState::Paused)
        play();
}

void PlayerCore::stop()
{
    positionTimer_->stop();
    stopAllThreads();
    audioPacketQueue_.flush();
    videoPacketQueue_.flush();
    audioFrameQueue_.flush();
    videoFrameQueue_.flush();
    if (audioOutput_) audioOutput_->stop();
    fallbackClock_.reset();
    demuxerFinished_ = false;
    emit timeChanged(0.0, 0.0);
    setState(PlayerState::Stopped);
}

void PlayerCore::seek(double seconds)
{
    if (duration_ <= 0.0)
        return;

    seconds = std::max(0.0, std::min(seconds, duration_));
    wasPausedBeforeSeek_ = (state_.load() == PlayerState::Paused);
    bool resumeAfterSeek = (state_.load() == PlayerState::Playing);
    positionTimer_->stop();
    setState(PlayerState::Seeking);
    demuxerFinished_ = false;
    emit seekStarted(seconds);
    emit timeChanged(seconds, duration_);

    if (audioOutput_)
        audioOutput_->flush(seconds, false);

    stopAllThreads();
    audioPacketQueue_.flush();
    videoPacketQueue_.flush();
    audioDecoder_.flush();
    videoDecoder_.flush();
    demuxer_.seekNow(seconds);

    audioPacketQueue_.reset();
    videoPacketQueue_.reset();
    audioFrameQueue_.reset();
    videoFrameQueue_.reset();
    fallbackClock_.setTime(seconds);
    if (audioOutput_)
        audioOutput_->discardBefore(seconds);

    demuxer_.start();
    audioDecoder_.start();
    videoDecoder_.start();

    if (resumeAfterSeek) {
        if (hasAudio_)
            audioOutput_->start();
        else if (hasVideo_)
            audioOutput_->audioClock().resume();
        positionTimer_->start();
        setState(PlayerState::Playing);
    } else {
        if (hasAudio_)
            audioOutput_->audioClock().pause();
        else if (hasVideo_)
            audioOutput_->audioClock().pause();
        setState(PlayerState::Paused);
    }
}

void PlayerCore::seekRelative(double deltaSeconds)
{
    double currentTime = audioClock().getTime();
    seek(currentTime + deltaSeconds);
}

void PlayerCore::setVolume(double volume)
{
    volume_ = std::max(0.0, std::min(volume, 1.0));
    if (audioOutput_ && !muted_)
        audioOutput_->setVolume(volume_);
}

void PlayerCore::setMuted(bool muted)
{
    muted_ = muted;
    if (audioOutput_)
        audioOutput_->setVolume(muted_ ? 0.0 : volume_);
}

void PlayerCore::setPlaybackRate(double rate)
{
    playbackRate_ = std::clamp(rate, 0.25, 4.0);
    fallbackClock_.setSpeed(playbackRate_);
    if (audioOutput_)
        audioOutput_->setPlaybackRate(playbackRate_);
}

void PlayerCore::setState(PlayerState s)
{
    PlayerState old = state_.exchange(s);
    if (old != s)
        emit stateChanged(s);
}

void PlayerCore::startPlayback()
{
    // Reset all queues
    audioPacketQueue_.reset();
    videoPacketQueue_.reset();
    audioFrameQueue_.reset();
    videoFrameQueue_.reset();
    fallbackClock_.reset();
    if (audioOutput_) {
        audioOutput_->audioClock().reset();
        audioOutput_->setPlaybackRate(playbackRate_);
        audioOutput_->setVolume(muted_ ? 0.0 : volume_);
    }

    // Start threads
    demuxer_.start();
    audioDecoder_.start();
    videoDecoder_.start();

    // Start audio output (or just the clock for video-only sync)
    if (hasAudio_)
        audioOutput_->start();
    else if (hasVideo_)
        audioOutput_->audioClock().resume();

    // Reset finished flag
    demuxerFinished_ = false;

    // Start position timer
    positionTimer_->start();

    setState(PlayerState::Playing);
}

void PlayerCore::stopAllThreads()
{
    audioPacketQueue_.abort();
    videoPacketQueue_.abort();
    audioFrameQueue_.abort();
    videoFrameQueue_.abort();

    demuxer_.stop();
    audioDecoder_.stop();
    videoDecoder_.stop();
}

void PlayerCore::cleanupResources()
{
    demuxer_.close();
    audioDecoder_.close();
    videoDecoder_.close();
    hasAudio_ = false;
    demuxerFinished_ = false;
    hasVideo_ = false;
    videoWidth_ = 0;
    videoHeight_ = 0;
    duration_ = 0.0;
}

void PlayerCore::onDemuxerFinished()
{
    demuxerFinished_ = true;
}

void PlayerCore::updatePosition()
{
    if (state_.load() == PlayerState::Playing) {
        double currentTime = audioClock().getTime();
        emit timeChanged(currentTime, duration_);

        // Check for true end-of-playback: demuxer done + all queues drained
        if (demuxerFinished_ &&
            audioPacketQueue_.packetCount() == 0 &&
            videoPacketQueue_.packetCount() == 0 &&
            audioFrameQueue_.size() == 0 &&
            videoFrameQueue_.size() == 0) {
            QMetaObject::invokeMethod(this, [this] {
                emit playbackFinished();
            }, Qt::QueuedConnection);
        }
    }
}
