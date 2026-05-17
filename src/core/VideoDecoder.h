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

class VideoDecoder {
public:
    VideoDecoder(PacketQueue& packetQueue, FrameQueue& frameQueue);
    ~VideoDecoder();

    bool open(AVCodecParameters* codecParams, AVRational streamTimeBase);
    void close();
    void start();
    void stop();
    void flush();

    int width() const { return width_; }
    int height() const { return height_; }
    int pixelFormat() const { return pixelFormat_; }
    AVRational frameRate() const;

private:
    void runLoop();

    PacketQueue& packetQueue_;
    FrameQueue& frameQueue_;
    AVCodecContext* codecCtx_ = nullptr;
    AVRational streamTimeBase_{1, 1};
    std::mutex codecMutex_;

    std::atomic<bool> running_{false};
    std::thread thread_;

    int width_ = 0;
    int height_ = 0;
    int pixelFormat_ = -1;
};
