#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstddef>

struct AVPacket;

class PacketQueue {
public:
    explicit PacketQueue(size_t maxByteSize = 15 * 1024 * 1024);
    ~PacketQueue();

    void push(AVPacket* pkt);
    AVPacket* pop(int timeoutMs = -1);
    AVPacket* tryPop();

    void abort();
    void reset();
    void flush();

    size_t packetCount() const;
    size_t byteSize() const;
    bool isAborted() const { return aborted_; }

private:
    std::queue<AVPacket*> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    size_t maxByteSize_;
    size_t byteSize_ = 0;
    std::atomic<bool> aborted_{false};
};
