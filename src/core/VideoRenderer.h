#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <memory>
#include <cstdint>

struct AVFrame;

class VideoRenderer {
public:
    VideoRenderer();
    ~VideoRenderer();

    void initialize(QOpenGLFunctions_3_3_Core* gl);
    void renderFrame(AVFrame* frame);
    void cleanup();

private:
    GLuint compileShader(GLenum type, const char* source);
    void setupGeometry();
    void uploadTextures(AVFrame* frame);
    void ensureTextures(int w, int h, int fmt);

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;

    GLuint yuvProgram_ = 0;
    GLuint nv12Program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;

    GLuint textures_[3] = {0, 0, 0};
    int texWidths_[3] = {0, 0, 0};
    int texHeights_[3] = {0, 0, 0};

    int videoWidth_ = 0;
    int videoHeight_ = 0;
    int pixelFormat_ = -1;

    bool initialized_ = false;
};
