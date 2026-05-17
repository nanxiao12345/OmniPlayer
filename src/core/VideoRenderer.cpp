#include "VideoRenderer.h"
#include <QFile>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

// Embedded shader sources (also available via Qt resources as fallback)
static const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

static const char* kFragmentShaderYUV420P = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D y_tex;
uniform sampler2D u_tex;
uniform sampler2D v_tex;
void main() {
    float y = texture(y_tex, TexCoord).r;
    float u = texture(u_tex, TexCoord).r - 0.5;
    float v = texture(v_tex, TexCoord).r - 0.5;
    float r = y + 1.402   * v;
    float g = y - 0.34414 * u - 0.71414 * v;
    float b = y + 1.772   * u;
    FragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
)";

static const char* kFragmentShaderNV12 = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D y_tex;
uniform sampler2D uv_tex;
void main() {
    float y = texture(y_tex, TexCoord).r;
    vec2 uv = texture(uv_tex, TexCoord).rg;
    float u = uv.r - 0.5;
    float v = uv.g - 0.5;
    float r = y + 1.402   * v;
    float g = y - 0.34414 * u - 0.71414 * v;
    float b = y + 1.772   * u;
    FragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
)";

VideoRenderer::VideoRenderer() = default;

VideoRenderer::~VideoRenderer()
{
    cleanup();
}

void VideoRenderer::initialize(QOpenGLFunctions_3_3_Core* gl)
{
    gl_ = gl;

    GLuint vert = compileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint yuvFrag = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderYUV420P);
    GLuint nv12Frag = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderNV12);

    yuvProgram_ = gl_->glCreateProgram();
    gl_->glAttachShader(yuvProgram_, vert);
    gl_->glAttachShader(yuvProgram_, yuvFrag);
    gl_->glLinkProgram(yuvProgram_);

    nv12Program_ = gl_->glCreateProgram();
    gl_->glAttachShader(nv12Program_, vert);
    gl_->glAttachShader(nv12Program_, nv12Frag);
    gl_->glLinkProgram(nv12Program_);

    gl_->glDeleteShader(vert);
    gl_->glDeleteShader(yuvFrag);
    gl_->glDeleteShader(nv12Frag);

    setupGeometry();
    initialized_ = true;
}

void VideoRenderer::cleanup()
{
    if (!gl_)
        return;
    if (yuvProgram_) gl_->glDeleteProgram(yuvProgram_);
    if (nv12Program_) gl_->glDeleteProgram(nv12Program_);
    if (vao_) gl_->glDeleteVertexArrays(1, &vao_);
    if (vbo_) gl_->glDeleteBuffers(1, &vbo_);
    if (ebo_) gl_->glDeleteBuffers(1, &ebo_);
    gl_->glDeleteTextures(3, textures_);
    yuvProgram_ = 0;
    nv12Program_ = 0;
    vao_ = 0;
    vbo_ = 0;
    ebo_ = 0;
    textures_[0] = textures_[1] = textures_[2] = 0;
    initialized_ = false;
}

GLuint VideoRenderer::compileShader(GLenum type, const char* source)
{
    GLuint shader = gl_->glCreateShader(type);
    gl_->glShaderSource(shader, 1, &source, nullptr);
    gl_->glCompileShader(shader);

    GLint success;
    gl_->glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        gl_->glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return shader;
}

void VideoRenderer::setupGeometry()
{
    float vertices[] = {
        // pos          // texcoord
        -1.0f,  1.0f,   0.0f, 0.0f,  // top-left
        -1.0f, -1.0f,   0.0f, 1.0f,  // bottom-left
         1.0f, -1.0f,   1.0f, 1.0f,  // bottom-right
         1.0f,  1.0f,   1.0f, 0.0f,  // top-right
    };
    unsigned indices[] = { 0, 1, 2, 0, 2, 3 };

    gl_->glGenVertexArrays(1, &vao_);
    gl_->glGenBuffers(1, &vbo_);
    gl_->glGenBuffers(1, &ebo_);

    gl_->glBindVertexArray(vao_);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl_->glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    gl_->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
        (void*)(2 * sizeof(float)));
    gl_->glEnableVertexAttribArray(1);

    gl_->glBindVertexArray(0);
}

void VideoRenderer::ensureTextures(int w, int h, int fmt)
{
    if (videoWidth_ == w && videoHeight_ == h && pixelFormat_ == fmt)
        return;

    videoWidth_ = w;
    videoHeight_ = h;
    pixelFormat_ = fmt;

    // Delete old textures
    gl_->glDeleteTextures(3, textures_);
    textures_[0] = textures_[1] = textures_[2] = 0;

    if (fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_YUVJ420P) {
        // Y plane
        gl_->glGenTextures(1, &textures_[0]);
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[0]);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        texWidths_[0] = w; texHeights_[0] = h;

        // U plane
        int uw = (w + 1) / 2, uh = (h + 1) / 2;
        gl_->glGenTextures(1, &textures_[1]);
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[1]);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, uw, uh, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        texWidths_[1] = uw; texHeights_[1] = uh;

        // V plane
        gl_->glGenTextures(1, &textures_[2]);
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[2]);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, uw, uh, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        texWidths_[2] = uw; texHeights_[2] = uh;
    } else if (fmt == AV_PIX_FMT_NV12) {
        // Y plane
        gl_->glGenTextures(1, &textures_[0]);
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[0]);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        texWidths_[0] = w; texHeights_[0] = h;

        // UV interleaved plane (RG)
        int uw = (w + 1) / 2, uh = (h + 1) / 2;
        gl_->glGenTextures(1, &textures_[1]);
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[1]);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, uw, uh, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
        texWidths_[1] = uw; texHeights_[1] = uh;
    }
}

void VideoRenderer::uploadTextures(AVFrame* frame)
{
    int fmt = frame->format;
    ensureTextures(frame->width, frame->height, fmt);

    if (fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_YUVJ420P) {
        // Y
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[0]);
        gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]);
        gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height,
            GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);

        // U
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[1]);
        gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1]);
        gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texWidths_[1], texHeights_[1],
            GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);

        // V
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[2]);
        gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[2]);
        gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texWidths_[2], texHeights_[2],
            GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);

        gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else if (fmt == AV_PIX_FMT_NV12) {
        // Y
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[0]);
        gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]);
        gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height,
            GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);

        // UV
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[1]);
        gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1] / 2);
        gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texWidths_[1], texHeights_[1],
            GL_RG, GL_UNSIGNED_BYTE, frame->data[1]);

        gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
}

void VideoRenderer::renderFrame(AVFrame* frame)
{
    if (!initialized_)
        return;

    int fmt = frame ? frame->format : AV_PIX_FMT_NONE;
    GLuint program = (fmt == AV_PIX_FMT_NV12) ? nv12Program_ : yuvProgram_;
    gl_->glUseProgram(program);
    gl_->glBindVertexArray(vao_);

    if (!frame || !frame->data[0]) {
        gl_->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl_->glClear(GL_COLOR_BUFFER_BIT);
        gl_->glBindVertexArray(0);
        return;
    }

    uploadTextures(frame);

    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, textures_[0]);
    gl_->glUniform1i(gl_->glGetUniformLocation(program, "y_tex"), 0);

    if (fmt == AV_PIX_FMT_NV12) {
        gl_->glActiveTexture(GL_TEXTURE1);
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[1]);
        gl_->glUniform1i(gl_->glGetUniformLocation(program, "uv_tex"), 1);
    } else {
        gl_->glActiveTexture(GL_TEXTURE1);
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[1]);
        gl_->glUniform1i(gl_->glGetUniformLocation(program, "u_tex"), 1);

        gl_->glActiveTexture(GL_TEXTURE2);
        gl_->glBindTexture(GL_TEXTURE_2D, textures_[2]);
        gl_->glUniform1i(gl_->glGetUniformLocation(program, "v_tex"), 2);
    }

    gl_->glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    gl_->glBindVertexArray(0);
    gl_->glUseProgram(0);
}
