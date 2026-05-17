#pragma once
#include <QObject>
#include <QAudioSink>
#include <QIODevice>
#include <QTimer>
#include <vector>
#include <atomic>
#include <mutex>
#include "Clock.h"

extern "C" {
#include <libavutil/samplefmt.h>
}

struct AVFrame;
struct SwrContext;
struct AVFilterGraph;
struct AVFilterContext;
class FrameQueue;

class AudioOutput : public QObject {
    Q_OBJECT
public:
    explicit AudioOutput(QObject* parent = nullptr);
    ~AudioOutput();

    bool init(int srcSampleRate, int srcChannels, int srcSampleFormat,
              FrameQueue& frameQueue);
    void start();
    void stop();
    void pause();
    void resume();
    void flush(double clockTime, bool keepPlaying);
    void discardBefore(double pts);
    void setVolume(double vol);
    void setPlaybackRate(double rate);

    Clock& audioClock() { return clock_; }

signals:
    void audioTimeUpdated(double pts);

private slots:
    void feedAudioData();

private:
    bool refillPcmBuffer();
    void updateClockFromDeviceBuffer();
    bool rebuildResampler();
    struct TempoFilterChain {
        AVFilterGraph* graph = nullptr;
        AVFilterContext* src = nullptr;
        AVFilterContext* sink = nullptr;
    };
    bool buildTempoFilterChain(double rate, TempoFilterChain& chain);
    void destroyTempoFilterChain(TempoFilterChain& chain);
    bool appendTempoFilteredPcm(const uint8_t* pcm, int samples, double pts);

    QAudioSink* audioSink_ = nullptr;
    QIODevice* audioDevice_ = nullptr;
    QTimer* feedTimer_ = nullptr;

    FrameQueue* frameQueue_ = nullptr;
    SwrContext* swrCtx_ = nullptr;
    TempoFilterChain tempoChain_;
    int64_t tempoInputSamples_ = 0;
    int64_t tempoOutputSamples_ = 0;

    int srcSampleRate_ = 0;
    int srcChannels_ = 0;
    AVSampleFormat srcFormat_ = AV_SAMPLE_FMT_NONE;

    int dstSampleRate_ = 44100;
    int dstChannels_ = 2;
    int bytesPerSecond_ = 0;
    double playbackRate_ = 1.0;

    AVFrame* currentFrame_ = nullptr;
    int currentSamplePos_ = 0;
    double accumulatedPts_ = 0.0;

    std::vector<uint8_t> resampleBuffer_;
    std::vector<uint8_t> pcmBuffer_;
    size_t pcmBufferOffset_ = 0;
    double pcmBufferPts_ = 0.0;
    double queuedEndPts_ = 0.0;
    double discardBeforePts_ = -1.0;

    Clock clock_;
    std::atomic<double> volume_{1.0};
    std::mutex mutex_;
};
