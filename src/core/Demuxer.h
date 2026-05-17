#pragma once
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <string>
#include "Types.h"

struct AVFormatContext;
struct AVCodecParameters;
class PacketQueue;

class Demuxer {
public:
    using FinishedCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string&)>;

    Demuxer(PacketQueue& audioQueue, PacketQueue& videoQueue);
    ~Demuxer();

    bool open(const std::string& filePath);
    void close();
    void start();
    void stop();
    void seek(double targetSeconds);
    bool seekNow(double targetSeconds);

    const StreamInfo& audioStreamInfo() const { return audioInfo_; }
    const StreamInfo& videoStreamInfo() const { return videoInfo_; }

    AVCodecParameters* audioCodecParams();
    AVCodecParameters* videoCodecParams();

    bool hasAudio() const { return audioInfo_.streamIndex >= 0; }
    bool hasVideo() const { return videoInfo_.streamIndex >= 0; }

    AVFormatContext* formatContext() const { return formatCtx_; }

    void setOnFinished(FinishedCallback cb) { onFinished_ = std::move(cb); }
    void setOnError(ErrorCallback cb) { onError_ = std::move(cb); }

private:
    void runLoop();
    static int interruptCallback(void* opaque);

    AVFormatContext* formatCtx_ = nullptr;
    PacketQueue& audioQueue_;
    PacketQueue& videoQueue_;
    StreamInfo audioInfo_;
    StreamInfo videoInfo_;
    int audioStreamIndex_ = -1;
    int videoStreamIndex_ = -1;

    std::atomic<bool> running_{false};
    std::atomic<bool> seeking_{false};
    std::atomic<bool> aborted_{false};
    std::atomic<double> seekTarget_{0.0};
    std::thread thread_;

    FinishedCallback onFinished_;
    ErrorCallback onError_;
    std::mutex callbackMutex_;

    std::string filePath_;
};
