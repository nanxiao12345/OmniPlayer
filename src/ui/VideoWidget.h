#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QTimer>
#include "../core/VideoRenderer.h"

class FrameQueue;
class Clock;

class VideoWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget();

    void setFrameQueue(FrameQueue* queue) { frameQueue_ = queue; }
    void setAudioClock(Clock* clock) { audioClock_ = clock; }
    void setSyncToAudio(bool sync) { syncToAudio_ = sync; }

    VideoRenderer* renderer() { return &renderer_; }
    void startRendering();
    void stopRendering();
    void clearFrame();
    void beginSeek(double targetSec);
    bool isSeekFramePending() const { return waitingForSeekFrame_; }

signals:
    void seekFrameReady();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private slots:
    void onRenderTick();

private:
    VideoRenderer renderer_;
    FrameQueue* frameQueue_ = nullptr;
    Clock* audioClock_ = nullptr;

    QTimer* renderTimer_ = nullptr;
    AVFrame* currentFrame_ = nullptr;
    bool syncToAudio_ = true;
    bool waitingForSeekFrame_ = false;
    double seekTargetSec_ = 0.0;
};
