#include "Demuxer.h"
#include "PacketQueue.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

Demuxer::Demuxer(PacketQueue& audioQueue, PacketQueue& videoQueue)
    : audioQueue_(audioQueue)
    , videoQueue_(videoQueue)
{
}

Demuxer::~Demuxer()
{
    close();
}

bool Demuxer::open(const std::string& filePath)
{
    close();

    aborted_ = false;
    audioInfo_ = StreamInfo{};
    videoInfo_ = StreamInfo{};
    audioStreamIndex_ = -1;
    videoStreamIndex_ = -1;
    formatCtx_ = avformat_alloc_context();
    if (!formatCtx_)
        return false;

    formatCtx_->interrupt_callback.callback = interruptCallback;
    formatCtx_->interrupt_callback.opaque = this;
    filePath_ = filePath;

    int ret = avformat_open_input(&formatCtx_, filePath.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errBuf[256] = {};
        av_strerror(ret, errBuf, sizeof(errBuf));
        fprintf(stderr, "avformat_open_input failed: %s (path=%s)\n", errBuf, filePath.c_str());
        avformat_free_context(formatCtx_);
        formatCtx_ = nullptr;
        return false;
    }

    ret = avformat_find_stream_info(formatCtx_, nullptr);
    if (ret < 0) {
        char errBuf[256] = {};
        av_strerror(ret, errBuf, sizeof(errBuf));
        fprintf(stderr, "avformat_find_stream_info failed: %s\n", errBuf);
        avformat_close_input(&formatCtx_);
        return false;
    }

    for (unsigned i = 0; i < formatCtx_->nb_streams; ++i) {
        AVStream* st = formatCtx_->streams[i];
        AVCodecParameters* par = st->codecpar;

        if (par->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex_ < 0) {
            audioStreamIndex_ = i;
            audioInfo_.streamIndex = i;
            audioInfo_.codecId = par->codec_id;
            audioInfo_.sampleRate = par->sample_rate;
            audioInfo_.channels = par->ch_layout.nb_channels;
            audioInfo_.sampleFormat = par->format;
            audioInfo_.timeBase = st->time_base;
            if (st->duration != AV_NOPTS_VALUE)
                audioInfo_.durationSec = st->duration * av_q2d(st->time_base);
        } else if (par->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex_ < 0) {
            videoStreamIndex_ = i;
            videoInfo_.streamIndex = i;
            videoInfo_.codecId = par->codec_id;
            videoInfo_.width = par->width;
            videoInfo_.height = par->height;
            videoInfo_.timeBase = st->time_base;
            if (st->duration != AV_NOPTS_VALUE)
                videoInfo_.durationSec = st->duration * av_q2d(st->time_base);
            else if (formatCtx_->duration != AV_NOPTS_VALUE)
                videoInfo_.durationSec = formatCtx_->duration / (double)AV_TIME_BASE;
        }
    }

    return hasAudio() || hasVideo();
}

AVCodecParameters* Demuxer::audioCodecParams()
{
    if (audioStreamIndex_ < 0 || !formatCtx_)
        return nullptr;
    return formatCtx_->streams[audioStreamIndex_]->codecpar;
}

AVCodecParameters* Demuxer::videoCodecParams()
{
    if (videoStreamIndex_ < 0 || !formatCtx_)
        return nullptr;
    return formatCtx_->streams[videoStreamIndex_]->codecpar;
}

void Demuxer::close()
{
    aborted_ = true;
    stop();
    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
    }
    audioInfo_ = StreamInfo{};
    videoInfo_ = StreamInfo{};
    audioStreamIndex_ = -1;
    videoStreamIndex_ = -1;
    filePath_.clear();
}

void Demuxer::start()
{
    if (running_)
        return;
    aborted_ = false;
    seeking_ = false;
    running_ = true;
    thread_ = std::thread(&Demuxer::runLoop, this);
}

void Demuxer::stop()
{
    running_ = false;
    aborted_ = true;
    if (thread_.joinable())
        thread_.join();
}

void Demuxer::seek(double targetSeconds)
{
    seeking_ = true;
    seekTarget_ = targetSeconds;
}

bool Demuxer::seekNow(double targetSeconds)
{
    if (!formatCtx_)
        return false;

    int64_t seekTarget = static_cast<int64_t>(targetSeconds * AV_TIME_BASE);
    int streamIdx = videoStreamIndex_ >= 0 ? videoStreamIndex_ : audioStreamIndex_;
    int ret = 0;
    if (streamIdx >= 0) {
        AVStream* st = formatCtx_->streams[streamIdx];
        int64_t streamTarget = av_rescale_q(seekTarget,
            AVRational{1, AV_TIME_BASE}, st->time_base);
        ret = av_seek_frame(formatCtx_, streamIdx, streamTarget, AVSEEK_FLAG_BACKWARD);
    } else {
        ret = av_seek_frame(formatCtx_, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
    }

    if (ret >= 0)
        avformat_flush(formatCtx_);
    return ret >= 0;
}

int Demuxer::interruptCallback(void* opaque)
{
    auto* self = static_cast<Demuxer*>(opaque);
    return self->aborted_ ? 1 : 0;
}

void Demuxer::runLoop()
{
    while (running_) {
        // Handle seek
        if (seeking_) {
            double target = seekTarget_;
            int64_t seekTarget = static_cast<int64_t>(target * AV_TIME_BASE);
            int streamIdx = videoStreamIndex_ >= 0 ? videoStreamIndex_ : audioStreamIndex_;
            if (streamIdx >= 0) {
                AVStream* st = formatCtx_->streams[streamIdx];
                int64_t streamTarget = av_rescale_q(seekTarget,
                    AVRational{1, AV_TIME_BASE}, st->time_base);
                av_seek_frame(formatCtx_, streamIdx, streamTarget, AVSEEK_FLAG_BACKWARD);
            } else {
                av_seek_frame(formatCtx_, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
            }
            // Flush internal buffers
            audioQueue_.flush();
            videoQueue_.flush();
            seeking_ = false;
        }

        AVPacket* pkt = av_packet_alloc();
        int ret = av_read_frame(formatCtx_, pkt);

        if (ret < 0) {
            av_packet_free(&pkt);
            if (ret == AVERROR_EOF) {
                if (audioStreamIndex_ >= 0) {
                    AVPacket* nullPkt = av_packet_alloc();
                    nullPkt->stream_index = audioStreamIndex_;
                    nullPkt->size = 0;
                    audioQueue_.push(nullPkt);
                }
                if (videoStreamIndex_ >= 0) {
                    AVPacket* nullPkt = av_packet_alloc();
                    nullPkt->stream_index = videoStreamIndex_;
                    nullPkt->size = 0;
                    videoQueue_.push(nullPkt);
                }
            }
            break;
        }

        if (pkt->stream_index == audioStreamIndex_) {
            audioQueue_.push(pkt);
        } else if (pkt->stream_index == videoStreamIndex_) {
            videoQueue_.push(pkt);
        } else {
            av_packet_free(&pkt);
        }
    }

    // Fire finished callback
    FinishedCallback cb;
    {
        std::lock_guard lock(callbackMutex_);
        cb = onFinished_;
    }
    if (cb) cb();
}
