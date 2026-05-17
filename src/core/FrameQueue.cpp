#include "FrameQueue.h"

extern "C" {
#include <libavutil/frame.h>
}

FrameQueue::FrameQueue(size_t maxSize)
    : maxSize_(maxSize)
{
}

FrameQueue::~FrameQueue()
{
    abort();
    flush();
}

void FrameQueue::push(AVFrame* frame)
{
    std::unique_lock lock(mutex_);
    cond_.wait(lock, [this] {
        return queue_.size() < maxSize_ || aborted_;
    });
    if (aborted_) {
        av_frame_free(&frame);
        return;
    }
    queue_.push_back(frame);
    cond_.notify_one();
}

AVFrame* FrameQueue::pop(int timeoutMs)
{
    std::unique_lock lock(mutex_);
    if (timeoutMs <= 0) {
        cond_.wait(lock, [this] { return !queue_.empty() || aborted_; });
    } else {
        cond_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
            return !queue_.empty() || aborted_;
        });
    }
    if (queue_.empty())
        return nullptr;
    AVFrame* frame = queue_.front();
    queue_.pop_front();
    cond_.notify_one();
    return frame;
}

AVFrame* FrameQueue::tryPop()
{
    std::lock_guard lock(mutex_);
    if (queue_.empty())
        return nullptr;
    AVFrame* frame = queue_.front();
    queue_.pop_front();
    cond_.notify_one();
    return frame;
}

AVFrame* FrameQueue::peekFront()
{
    std::unique_lock lock(mutex_);
    while (queue_.empty() && !aborted_) {
        cond_.wait(lock);
    }
    return queue_.empty() ? nullptr : queue_.front();
}

AVFrame* FrameQueue::tryPeekFront()
{
    std::lock_guard lock(mutex_);
    return queue_.empty() ? nullptr : queue_.front();
}

double FrameQueue::frontPts() const
{
    std::lock_guard lock(mutex_);
    if (queue_.empty())
        return -1.0;
    AVFrame* f = queue_.front();
    if (f->pts == AV_NOPTS_VALUE)
        return -1.0;
    AVRational tb = f->time_base.num != 0
        ? f->time_base
        : AVRational{1, 1};
    return f->pts * av_q2d(tb);
}

void FrameQueue::abort()
{
    aborted_ = true;
    std::lock_guard lock(mutex_);
    cond_.notify_all();
}

void FrameQueue::reset()
{
    aborted_ = false;
}

void FrameQueue::flush()
{
    std::lock_guard lock(mutex_);
    for (auto* frame : queue_)
        av_frame_free(&frame);
    queue_.clear();
    cond_.notify_all();
}

size_t FrameQueue::size() const
{
    std::lock_guard lock(mutex_);
    return queue_.size();
}
