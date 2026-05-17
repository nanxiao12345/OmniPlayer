#include "PacketQueue.h"

extern "C" {
#include <libavcodec/packet.h>
}

PacketQueue::PacketQueue(size_t maxByteSize)
    : maxByteSize_(maxByteSize)
{
}

PacketQueue::~PacketQueue()
{
    abort();
    flush();
}

void PacketQueue::push(AVPacket* pkt)
{
    std::unique_lock lock(mutex_);
    cond_.wait(lock, [this] {
        return byteSize_ < maxByteSize_ || aborted_;
    });
    if (aborted_) {
        av_packet_free(&pkt);
        return;
    }
    queue_.push(pkt);
    byteSize_ += pkt->size;
    cond_.notify_one();
}

AVPacket* PacketQueue::pop(int timeoutMs)
{
    std::unique_lock lock(mutex_);
    if (timeoutMs <= 0) {
        cond_.wait(lock, [this] { return !queue_.empty() || aborted_; });
    } else {
        cond_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
            return !queue_.empty() || aborted_;
        });
    }
    if (aborted_ && queue_.empty())
        return nullptr;
    if (queue_.empty())
        return nullptr;
    AVPacket* pkt = queue_.front();
    queue_.pop();
    byteSize_ -= pkt->size;
    cond_.notify_one();
    return pkt;
}

AVPacket* PacketQueue::tryPop()
{
    std::lock_guard lock(mutex_);
    if (queue_.empty())
        return nullptr;
    AVPacket* pkt = queue_.front();
    queue_.pop();
    byteSize_ -= pkt->size;
    cond_.notify_one();
    return pkt;
}

void PacketQueue::abort()
{
    aborted_ = true;
    std::lock_guard lock(mutex_);
    cond_.notify_all();
}

void PacketQueue::reset()
{
    aborted_ = false;
}

void PacketQueue::flush()
{
    std::lock_guard lock(mutex_);
    while (!queue_.empty()) {
        av_packet_free(&queue_.front());
        queue_.pop();
    }
    byteSize_ = 0;
    cond_.notify_all();
}

size_t PacketQueue::packetCount() const
{
    std::lock_guard lock(mutex_);
    return queue_.size();
}

size_t PacketQueue::byteSize() const
{
    return byteSize_;
}
