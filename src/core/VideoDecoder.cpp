#include "VideoDecoder.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

VideoDecoder::VideoDecoder(PacketQueue& packetQueue, FrameQueue& frameQueue)
    : packetQueue_(packetQueue)
    , frameQueue_(frameQueue)
{
}

VideoDecoder::~VideoDecoder()
{
    close();
}

bool VideoDecoder::open(AVCodecParameters* codecParams, AVRational streamTimeBase)
{
    if (!codecParams)
        return false;

    close();

    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec)
        return false;

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_)
        return false;

    if (avcodec_parameters_to_context(codecCtx_, codecParams) < 0) {
        avcodec_free_context(&codecCtx_);
        return false;
    }

    streamTimeBase_ = streamTimeBase.num != 0 ? streamTimeBase : AVRational{1, 90000};
    codecCtx_->pkt_timebase = streamTimeBase_;
    codecCtx_->thread_count = 0;

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx_);
        return false;
    }

    width_ = codecCtx_->width;
    height_ = codecCtx_->height;
    pixelFormat_ = codecCtx_->pix_fmt;

    return true;
}

void VideoDecoder::close()
{
    stop();
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
}

void VideoDecoder::start()
{
    if (running_)
        return;
    running_ = true;
    thread_ = std::thread(&VideoDecoder::runLoop, this);
}

void VideoDecoder::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

void VideoDecoder::flush()
{
    std::lock_guard lock(codecMutex_);
    if (codecCtx_)
        avcodec_flush_buffers(codecCtx_);
    frameQueue_.flush();
}

AVRational VideoDecoder::frameRate() const
{
    if (codecCtx_ && codecCtx_->framerate.num > 0)
        return codecCtx_->framerate;
    return AVRational{25, 1};
}

void VideoDecoder::runLoop()
{
    while (running_) {
        AVPacket* pkt = packetQueue_.pop(100);
        if (!pkt) {
            if (packetQueue_.isAborted())
                break;
            continue;
        }

        bool isEof = (pkt->size == 0);
        std::vector<AVFrame*> decodedFrames;
        int ret = 0;
        {
            std::lock_guard lock(codecMutex_);
            ret = avcodec_send_packet(codecCtx_, isEof ? nullptr : pkt);
        }
        av_packet_free(&pkt);

        if (ret < 0 && ret != AVERROR_EOF)
            continue;

        while (true) {
            AVFrame* frame = av_frame_alloc();
            {
                std::lock_guard lock(codecMutex_);
                ret = avcodec_receive_frame(codecCtx_, frame);
            }
            if (ret == 0) {
                if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                    frame->pts = frame->best_effort_timestamp;
                frame->time_base = streamTimeBase_;
                decodedFrames.push_back(frame);
            } else {
                av_frame_free(&frame);
                break;
            }
        }

        for (AVFrame* frame : decodedFrames)
            frameQueue_.push(frame);
    }
}
