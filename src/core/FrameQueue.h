#pragma once
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstddef>

struct AVFrame;

class FrameQueue {
public:
    explicit FrameQueue(size_t maxSize = 3);
    ~FrameQueue();

    void push(AVFrame* frame);
    AVFrame* pop(int timeoutMs = -1);
    AVFrame* tryPop();

    AVFrame* peekFront();
    AVFrame* tryPeekFront();

    double frontPts() const;

    void abort();
    void reset();
    void flush();

    size_t size() const;
    bool isAborted() const { return aborted_; }

private:
    std::deque<AVFrame*> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    size_t maxSize_;
    std::atomic<bool> aborted_{false};
};
