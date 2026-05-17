#include "AudioDecoder.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

AudioDecoder::AudioDecoder(PacketQueue& packetQueue, FrameQueue& frameQueue)
    : packetQueue_(packetQueue)
    , frameQueue_(frameQueue)
{
}

AudioDecoder::~AudioDecoder()
{
    close();
}

bool AudioDecoder::open(AVCodecParameters* codecParams, AVRational streamTimeBase)
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

    streamTimeBase_ = streamTimeBase.num != 0 ? streamTimeBase : AVRational{1, codecCtx_->sample_rate};
    codecCtx_->pkt_timebase = streamTimeBase_;
    codecCtx_->thread_count = 0; // auto

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx_);
        return false;
    }

    sampleRate_ = codecCtx_->sample_rate;
    channels_ = codecCtx_->ch_layout.nb_channels;
    sampleFormat_ = codecCtx_->sample_fmt;
    channelLayout_ = codecCtx_->ch_layout.u.mask;

    return true;
}

void AudioDecoder::close()
{
    stop();
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
}

void AudioDecoder::start()
{
    if (running_)
        return;
    running_ = true;
    thread_ = std::thread(&AudioDecoder::runLoop, this);
}

void AudioDecoder::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

void AudioDecoder::flush()
{
    std::lock_guard lock(codecMutex_);
    if (codecCtx_)
        avcodec_flush_buffers(codecCtx_);
    frameQueue_.flush();
}

void AudioDecoder::runLoop()
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
