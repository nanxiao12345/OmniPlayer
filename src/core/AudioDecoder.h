#pragma once
#include <thread>
#include <atomic>
#include <cstdint>
#include <mutex>

extern "C" {
#include <libavutil/rational.h>
}

struct AVCodecContext;
struct AVCodecParameters;
class PacketQueue;
class FrameQueue;

class AudioDecoder {
public:
    AudioDecoder(PacketQueue& packetQueue, FrameQueue& frameQueue);
    ~AudioDecoder();

    bool open(AVCodecParameters* codecParams, AVRational streamTimeBase);
    void close();
    void start();
    void stop();
    void flush();

    int sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }
    int sampleFormat() const { return sampleFormat_; }
    int64_t channelLayout() const { return channelLayout_; }

private:
    void runLoop();

    PacketQueue& packetQueue_;
    FrameQueue& frameQueue_;
    AVCodecContext* codecCtx_ = nullptr;
    AVRational streamTimeBase_{1, 1};
    std::mutex codecMutex_;

    std::atomic<bool> running_{false};
    std::thread thread_;

    int sampleRate_ = 0;
    int channels_ = 0;
    int sampleFormat_ = -1;
    int64_t channelLayout_ = 0;
};
