#include "AudioOutput.h"
#include "FrameQueue.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}

AudioOutput::AudioOutput(QObject* parent)
    : QObject(parent)
{
}

AudioOutput::~AudioOutput()
{
    stop();
    if (swrCtx_)
        swr_free(&swrCtx_);
    destroyTempoFilterChain(tempoChain_);
}

bool AudioOutput::init(int srcSampleRate, int srcChannels, int srcSampleFormat,
                        FrameQueue& frameQueue)
{
    stop();
    if (audioSink_) {
        delete audioSink_;
        audioSink_ = nullptr;
    }
    audioDevice_ = nullptr;
    if (feedTimer_) {
        delete feedTimer_;
        feedTimer_ = nullptr;
    }
    if (swrCtx_)
        swr_free(&swrCtx_);
    destroyTempoFilterChain(tempoChain_);

    frameQueue_ = &frameQueue;
    srcSampleRate_ = srcSampleRate;
    srcChannels_ = srcChannels;
    srcFormat_ = static_cast<AVSampleFormat>(srcSampleFormat);

    dstSampleRate_ = srcSampleRate > 0 ? srcSampleRate : 44100;

    if (!rebuildResampler())
        return false;
    if (!buildTempoFilterChain(playbackRate_, tempoChain_))
        return false;

    // Build QAudioFormat and validate
    QAudioFormat fmt;
    fmt.setSampleRate(dstSampleRate_);
    fmt.setChannelCount(dstChannels_);
    fmt.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    if (!defaultDevice.isFormatSupported(fmt)) {
        // Try common sample rates until one is supported
        int fallbackRates[] = { 48000, 44100, 22050, 16000, 8000 };
        bool found = false;
        for (int rate : fallbackRates) {
            if (rate == dstSampleRate_)
                continue;
            fmt.setSampleRate(rate);
            if (defaultDevice.isFormatSupported(fmt)) {
                dstSampleRate_ = rate;
                found = true;
                break;
            }
        }
        if (!found)
            return false;
        if (!rebuildResampler())
            return false;
        if (!buildTempoFilterChain(playbackRate_, tempoChain_))
            return false;
    }

    bytesPerSecond_ = dstSampleRate_ * dstChannels_ * static_cast<int>(sizeof(int16_t));
    audioSink_ = new QAudioSink(defaultDevice, fmt, this);
    audioSink_->setBufferSize(bytesPerSecond_ / 5);
    // Don't start yet; defer to start() so data is already flowing.
    connect(audioSink_, &QAudioSink::stateChanged, this, [this](QAudio::State) {
        if (audioSink_->error() != QAudio::NoError)
            fprintf(stderr, "AudioOutput: QAudioSink error %d\n", audioSink_->error());
    });

    feedTimer_ = new QTimer(this);
    connect(feedTimer_, &QTimer::timeout, this, &AudioOutput::feedAudioData);
    feedTimer_->setInterval(10);

    resampleBuffer_.clear();
    pcmBuffer_.clear();
    pcmBufferOffset_ = 0;
    pcmBufferPts_ = 0.0;
    queuedEndPts_ = 0.0;
    accumulatedPts_ = 0.0;

    return true;
}

void AudioOutput::start()
{
    if (audioSink_) {
        if (audioSink_->state() != QAudio::ActiveState) {
            audioDevice_ = audioSink_->start();
            if (!audioDevice_) {
                fprintf(stderr, "AudioOutput: QAudioSink::start() returned null\n");
            }
        }
    }
    if (feedTimer_)
        feedTimer_->start();
    clock_.resume();
}

void AudioOutput::stop()
{
    if (feedTimer_)
        feedTimer_->stop();
    if (audioSink_)
        audioSink_->stop();
    clock_.reset();
    accumulatedPts_ = 0.0;
    {
        std::lock_guard lock(mutex_);
        if (currentFrame_) {
            av_frame_free(&currentFrame_);
        currentFrame_ = nullptr;
        }
        currentSamplePos_ = 0;
        pcmBuffer_.clear();
        pcmBufferOffset_ = 0;
        pcmBufferPts_ = 0.0;
        queuedEndPts_ = 0.0;
    }
}

void AudioOutput::pause()
{
    clock_.pause();
    if (feedTimer_)
        feedTimer_->stop();
    if (audioSink_)
        audioSink_->suspend();
}

void AudioOutput::resume()
{
    if (audioSink_) {
        if (audioSink_->state() == QAudio::StoppedState) {
            audioDevice_ = audioSink_->start();
        } else {
            audioSink_->resume();
        }
    }
    if (feedTimer_)
        feedTimer_->start();
    clock_.resume();
}

void AudioOutput::flush(double clockTime, bool keepPlaying)
{
    stop();
    rebuildResampler();
    buildTempoFilterChain(playbackRate_, tempoChain_);
    clock_.setTime(clockTime);
    if (keepPlaying)
        start();
}

void AudioOutput::discardBefore(double pts)
{
    std::lock_guard lock(mutex_);
    discardBeforePts_ = pts;
}

void AudioOutput::setPlaybackRate(double rate)
{
    rate = std::clamp(rate, 0.25, 4.0);
    std::lock_guard lock(mutex_);

    TempoFilterChain newChain;
    if (!buildTempoFilterChain(rate, newChain)) {
        fprintf(stderr, "AudioOutput: failed to build tempo filter for rate %.3f\n", rate);
        return;
    }

    destroyTempoFilterChain(tempoChain_);
    tempoChain_ = newChain;
    playbackRate_ = rate;
    pcmBuffer_.clear();
    pcmBufferOffset_ = 0;
    clock_.setSpeed(rate);
}

void AudioOutput::setVolume(double vol)
{
    volume_ = vol;
    if (audioSink_)
        audioSink_->setVolume(vol);
}

void AudioOutput::feedAudioData()
{
    if (!frameQueue_ || !swrCtx_ || !audioSink_)
        return;

    // Recover from idle/error state
    QAudio::State st = audioSink_->state();
    if (audioSink_->error() != QAudio::NoError) {
        audioSink_->stop();
        audioDevice_ = audioSink_->start();
        if (!audioDevice_)
            return;
    } else if (st == QAudio::IdleState) {
        if (frameQueue_->size() > 0) {
            // Don't overwrite audioDevice_ — the one from initial start() is valid.
            // QAudioSink::start() may return null in IdleState on some Qt builds.
            if (!audioDevice_)
                audioDevice_ = audioSink_->start();
            if (!audioDevice_)
                return;
        } else {
            return;
        }
    } else if (st != QAudio::ActiveState) {
        return;
    }

    if (!audioDevice_)
        return;

    std::lock_guard lock(mutex_);

    // Determine how many bytes the audio device wants
    int bytesFree = audioSink_->bytesFree();
    if (bytesFree <= 0)
        return;

    int totalWritten = 0;

    while (totalWritten < bytesFree) {
        if (pcmBufferOffset_ >= pcmBuffer_.size()) {
            if (!refillPcmBuffer())
                break;
        }

        int available = static_cast<int>(pcmBuffer_.size() - pcmBufferOffset_);
        int toWrite = std::min(bytesFree - totalWritten, available);
        qint64 written = audioDevice_->write(
            reinterpret_cast<const char*>(pcmBuffer_.data() + pcmBufferOffset_),
            toWrite);
        if (written <= 0)
            break;

        // Track consumed input samples (≈ output samples for same-rate; scaled otherwise)
        pcmBufferOffset_ += static_cast<size_t>(written);
        totalWritten += static_cast<int>(written);
        queuedEndPts_ = pcmBufferPts_
            + static_cast<double>(pcmBufferOffset_) / bytesPerSecond_ * playbackRate_;

        // Update clock with current PTS
            // PTS missing from decoder — estimate from consumed samples
    }

    if (totalWritten > 0)
        updateClockFromDeviceBuffer();
}

bool AudioOutput::refillPcmBuffer()
{
    AVFrame* frame = frameQueue_->tryPop();
    while (frame) {
        if (frame->pts == AV_NOPTS_VALUE || discardBeforePts_ < 0.0)
            break;

        double framePts = frame->pts * av_q2d(frame->time_base);
        double frameDuration = frame->nb_samples > 0 && srcSampleRate_ > 0
            ? static_cast<double>(frame->nb_samples) / srcSampleRate_
            : 0.0;
        if (framePts + frameDuration >= discardBeforePts_ - 0.02)
            break;

        av_frame_free(&frame);
        frame = frameQueue_->tryPop();
    }

    if (!frame)
        return false;

    int inRate = srcSampleRate_ > 0 ? srcSampleRate_ : dstSampleRate_;
    int outSamplesMax = static_cast<int>(av_rescale_rnd(
        swr_get_delay(swrCtx_, inRate) + frame->nb_samples,
        dstSampleRate_, inRate, AV_ROUND_UP));
    if (outSamplesMax <= 0) {
        av_frame_free(&frame);
        return false;
    }

    resampleBuffer_.resize(outSamplesMax * dstChannels_ * sizeof(int16_t));
    uint8_t* outPtr = resampleBuffer_.data();
    int outSamples = swr_convert(swrCtx_,
        &outPtr, outSamplesMax,
        const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);

    if (outSamples <= 0) {
        av_frame_free(&frame);
        return false;
    }

    if (frame->pts != AV_NOPTS_VALUE) {
        pcmBufferPts_ = frame->pts * av_q2d(frame->time_base);
        accumulatedPts_ = pcmBufferPts_;
        if (discardBeforePts_ >= 0.0 && pcmBufferPts_ >= discardBeforePts_ - 0.02)
            discardBeforePts_ = -1.0;
    } else {
        pcmBufferPts_ = accumulatedPts_;
    }

    if (!appendTempoFilteredPcm(resampleBuffer_.data(), outSamples, pcmBufferPts_)) {
        av_frame_free(&frame);
        return false;
    }

    pcmBufferOffset_ = 0;
    accumulatedPts_ = pcmBufferPts_
        + static_cast<double>(pcmBuffer_.size()) / bytesPerSecond_ * playbackRate_;

    av_frame_free(&frame);
    return true;
}

void AudioOutput::updateClockFromDeviceBuffer()
{
    if (!audioSink_ || bytesPerSecond_ <= 0)
        return;

    qint64 bufferedBytes = std::max<qint64>(
        0, audioSink_->bufferSize() - audioSink_->bytesFree());
    double bufferedSec = static_cast<double>(bufferedBytes) / bytesPerSecond_;
    clock_.setTime(std::max(0.0, queuedEndPts_ - bufferedSec * playbackRate_));
}

bool AudioOutput::rebuildResampler()
{
    if (srcChannels_ <= 0 || srcSampleRate_ <= 0 || srcFormat_ == AV_SAMPLE_FMT_NONE)
        return false;

    if (swrCtx_)
        swr_free(&swrCtx_);

    AVChannelLayout srcLayout;
    av_channel_layout_default(&srcLayout, srcChannels_);
    AVChannelLayout dstLayout;
    av_channel_layout_default(&dstLayout, dstChannels_);

    swr_alloc_set_opts2(&swrCtx_,
        &dstLayout, AV_SAMPLE_FMT_S16, dstSampleRate_,
        &srcLayout, srcFormat_, srcSampleRate_,
        0, nullptr);

    return swrCtx_ && swr_init(swrCtx_) >= 0;
}

void AudioOutput::destroyTempoFilterChain(TempoFilterChain& chain)
{
    if (chain.graph)
        avfilter_graph_free(&chain.graph);
    chain.src = nullptr;
    chain.sink = nullptr;
}

bool AudioOutput::buildTempoFilterChain(double rate, TempoFilterChain& chain)
{
    destroyTempoFilterChain(chain);

    // If rate is ~1.0, no filter needed — copy PCM through directly
    if (std::abs(rate - 1.0) < 0.001) {
        tempoInputSamples_ = 0;
        tempoOutputSamples_ = 0;
        return true;
    }

    const AVFilter* abuffer = avfilter_get_by_name("abuffer");
    const AVFilter* atempo = avfilter_get_by_name("atempo");
    const AVFilter* aformat = avfilter_get_by_name("aformat");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffer || !atempo || !aformat || !abuffersink)
        return false;

    AVChannelLayout layout;
    av_channel_layout_default(&layout, dstChannels_);
    char layoutName[64] = {};
    av_channel_layout_describe(&layout, layoutName, sizeof(layoutName));

    chain.graph = avfilter_graph_alloc();
    if (!chain.graph)
        return false;

    char srcArgs[256] = {};
    std::snprintf(srcArgs, sizeof(srcArgs),
        "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
        dstSampleRate_, dstSampleRate_, av_get_sample_fmt_name(AV_SAMPLE_FMT_S16), layoutName);

    if (avfilter_graph_create_filter(&chain.src, abuffer, "in", srcArgs, nullptr, chain.graph) < 0) {
        destroyTempoFilterChain(chain);
        return false;
    }

    // Chain atempo filters. atempo only supports [0.5, 2.0].
    // For rates outside that range, cascade multiple atempo filters.
    AVFilterContext* prev = chain.src;
    double remaining = rate;
    int filterIdx = 0;

    while (remaining < 0.499 || remaining > 2.001) {
        double step = (remaining < 1.0) ? 0.5 : 2.0;
        char stepArgs[64] = {};
        std::snprintf(stepArgs, sizeof(stepArgs), "tempo=%.6f", step);
        char name[32] = {};
        std::snprintf(name, sizeof(name), "atempo_%d", filterIdx);

        AVFilterContext* stepCtx = nullptr;
        if (avfilter_graph_create_filter(&stepCtx, atempo, name, stepArgs, nullptr, chain.graph) < 0) {
            destroyTempoFilterChain(chain);
            return false;
        }
        if (avfilter_link(prev, 0, stepCtx, 0) < 0) {
            destroyTempoFilterChain(chain);
            return false;
        }
        prev = stepCtx;
        remaining /= step;
        filterIdx++;
    }

    // Final atempo for the remaining rate (if not ~1.0)
    if (std::abs(remaining - 1.0) >= 0.001) {
        char finalArgs[64] = {};
        std::snprintf(finalArgs, sizeof(finalArgs), "tempo=%.6f", remaining);
        char name[32] = {};
        std::snprintf(name, sizeof(name), "atempo_%d", filterIdx);

        AVFilterContext* finalCtx = nullptr;
        if (avfilter_graph_create_filter(&finalCtx, atempo, name, finalArgs, nullptr, chain.graph) < 0) {
            destroyTempoFilterChain(chain);
            return false;
        }
        if (avfilter_link(prev, 0, finalCtx, 0) < 0) {
            destroyTempoFilterChain(chain);
            return false;
        }
        prev = finalCtx;
        filterIdx++;
    }

    // aformat to ensure consistent output format
    char fmtArgs[128] = {};
    std::snprintf(fmtArgs, sizeof(fmtArgs),
        "sample_fmts=%s:sample_rates=%d:channel_layouts=%s",
        av_get_sample_fmt_name(AV_SAMPLE_FMT_S16), dstSampleRate_, layoutName);
    AVFilterContext* formatCtx = nullptr;
    if (avfilter_graph_create_filter(&formatCtx, aformat, "outfmt", fmtArgs, nullptr, chain.graph) < 0) {
        destroyTempoFilterChain(chain);
        return false;
    }
    if (avfilter_link(prev, 0, formatCtx, 0) < 0) {
        destroyTempoFilterChain(chain);
        return false;
    }

    // abuffersink
    if (avfilter_graph_create_filter(&chain.sink, abuffersink, "out", nullptr, nullptr, chain.graph) < 0) {
        destroyTempoFilterChain(chain);
        return false;
    }
    if (avfilter_link(formatCtx, 0, chain.sink, 0) < 0) {
        destroyTempoFilterChain(chain);
        return false;
    }

    if (avfilter_graph_config(chain.graph, nullptr) < 0) {
        destroyTempoFilterChain(chain);
        return false;
    }

    tempoInputSamples_ = 0;
    tempoOutputSamples_ = 0;
    return true;
}

bool AudioOutput::appendTempoFilteredPcm(const uint8_t* pcm, int samples, double pts)
{
    int bytesPerSample = dstChannels_ * static_cast<int>(sizeof(int16_t));

    // No tempo filter needed for rate 1.0 — copy PCM directly
    if (!tempoChain_.graph) {
        pcmBuffer_.clear();
        pcmBuffer_.resize(samples * bytesPerSample);
        std::memcpy(pcmBuffer_.data(), pcm, pcmBuffer_.size());
        pcmBufferPts_ = pts;
        tempoOutputSamples_ += samples;
        return true;
    }

    pcmBuffer_.clear();

    AVFrame* inFrame = av_frame_alloc();
    if (!inFrame)
        return false;

    av_channel_layout_default(&inFrame->ch_layout, dstChannels_);
    inFrame->format = AV_SAMPLE_FMT_S16;
    inFrame->sample_rate = dstSampleRate_;
    inFrame->nb_samples = samples;
    inFrame->pts = static_cast<int64_t>(pts * dstSampleRate_);
    if (av_frame_get_buffer(inFrame, 0) < 0) {
        av_frame_free(&inFrame);
        return false;
    }

    int inBytes = samples * bytesPerSample;
    std::memcpy(inFrame->data[0], pcm, inBytes);

    if (av_buffersrc_add_frame_flags(tempoChain_.src, inFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        av_frame_free(&inFrame);
        return false;
    }
    av_frame_free(&inFrame);

    tempoInputSamples_ += samples;

    // Drain all available output from the tempo filter chain.
    // PTS of the first output sample is computed from cumulative sample accounting:
    // tempoOutputSamples_ output samples already produced equal
    // tempoOutputSamples_ * playbackRate_ input-equivalent samples consumed.
    // The first unconsumed input sample's PTS is our output start PTS.
    int totalOutSamples = 0;
    while (true) {
        AVFrame* outFrame = av_frame_alloc();
        int ret = av_buffersink_get_frame(tempoChain_.sink, outFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&outFrame);
            break;
        }
        if (ret < 0) {
            av_frame_free(&outFrame);
            return pcmBuffer_.empty() ? false : true;
        }

        int outBytes = outFrame->nb_samples * bytesPerSample;
        if (pcmBuffer_.empty()) {
            double consumedInputSec = static_cast<double>(
                static_cast<int64_t>(tempoOutputSamples_) * playbackRate_) / dstSampleRate_;
            double priorInputSec = static_cast<double>(tempoInputSamples_ - samples) / dstSampleRate_;
            pcmBufferPts_ = pts - priorInputSec + consumedInputSec;
        }

        size_t oldSize = pcmBuffer_.size();
        pcmBuffer_.resize(oldSize + outBytes);
        std::memcpy(pcmBuffer_.data() + oldSize, outFrame->data[0], outBytes);
        totalOutSamples += outFrame->nb_samples;
        av_frame_free(&outFrame);
    }

    tempoOutputSamples_ += totalOutSamples;
    return !pcmBuffer_.empty();
}
