#include "VideoWidget.h"
#include "../core/FrameQueue.h"
#include "../core/Clock.h"
#include "../core/Types.h"
#include <QOpenGLVersionFunctionsFactory>

extern "C" {
#include <libavutil/frame.h>
}

VideoWidget::VideoWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    renderTimer_ = new QTimer(this);
    connect(renderTimer_, &QTimer::timeout, this, &VideoWidget::onRenderTick);
    renderTimer_->setInterval(16);
}

VideoWidget::~VideoWidget()
{
    makeCurrent();
    renderer_.cleanup();
    doneCurrent();

    if (currentFrame_) {
        av_frame_free(&currentFrame_);
        currentFrame_ = nullptr;
    }
}

void VideoWidget::initializeGL()
{
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(context());
    if (gl) {
        renderer_.initialize(gl);
    }
}

void VideoWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void VideoWidget::paintGL()
{
    makeCurrent();
    renderer_.renderFrame(currentFrame_);
}

void VideoWidget::onRenderTick()
{
    if (!frameQueue_ || !audioClock_)
        return;

    if (waitingForSeekFrame_) {
        while (true) {
            AVFrame* frame = frameQueue_->tryPeekFront();
            if (!frame)
                break;

            double pts = frame->pts == AV_NOPTS_VALUE
                ? seekTargetSec_
                : frame->pts * av_q2d(frame->time_base);
            if (pts < seekTargetSec_ - 0.05) {
                AVFrame* dropped = frameQueue_->tryPop();
                if (dropped)
                    av_frame_free(&dropped);
                continue;
            }

            AVFrame* popped = frameQueue_->tryPop();
            if (popped) {
                if (currentFrame_)
                    av_frame_free(&currentFrame_);
                currentFrame_ = popped;
                waitingForSeekFrame_ = false;
                update();
                emit seekFrameReady();
            }
            return;
        }
        update();
        return;
    }

    if (!syncToAudio_) {
        // Video-only: render frames as they arrive, update clock with frame PTS
        AVFrame* frame = frameQueue_->tryPop();
        if (frame) {
            if (currentFrame_)
                av_frame_free(&currentFrame_);
            currentFrame_ = frame;
            double pts = frame->pts * av_q2d(frame->time_base);
            audioClock_->setTime(pts);
        }
        update();
        return;
    }

    // Audio-video sync mode
    double audioTime = audioClock_->getTime();

    while (true) {
        AVFrame* frame = frameQueue_->tryPeekFront();
        if (!frame)
            break;

        double pts = frame->pts * av_q2d(frame->time_base);
        double diff = pts - audioTime;

        if (diff > AV_SYNC_THRESHOLD_SEC)
            break;

        if (diff < -AV_NOSYNC_THRESHOLD_SEC) {
            AVFrame* dropped = frameQueue_->tryPop();
            if (dropped)
                av_frame_free(&dropped);
            continue;
        }

        AVFrame* popped = frameQueue_->tryPop();
        if (popped) {
            if (currentFrame_)
                av_frame_free(&currentFrame_);
            currentFrame_ = popped;
        }
        break;
    }

    update();
}

void VideoWidget::startRendering()
{
    if (renderTimer_)
        renderTimer_->start();
}

void VideoWidget::stopRendering()
{
    if (renderTimer_)
        renderTimer_->stop();
}

void VideoWidget::clearFrame()
{
    if (currentFrame_) {
        av_frame_free(&currentFrame_);
        currentFrame_ = nullptr;
    }
    waitingForSeekFrame_ = false;
    update();
}

void VideoWidget::beginSeek(double targetSec)
{
    seekTargetSec_ = targetSec;
    waitingForSeekFrame_ = true;
    startRendering();
    update();
}
