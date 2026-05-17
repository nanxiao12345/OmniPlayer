# MediaPlayer

一个基于 **Qt 6** + **FFmpeg** + **OpenGL** 的高性能跨平台媒体播放器，采用多线程流水线架构，支持硬件加速渲染和精准的音视频同步。

## 项目简介

MediaPlayer 是一个从零构建的桌面媒体播放器，旨在解决以下问题：

- **低延迟解码**：基于 FFmpeg 的多线程解码流水线（解封装 → 音频解码 → 视频解码），充分利用多核 CPU。
- **精准音视频同步**：以音频时钟为基准，实现毫秒级（10ms）的视音频同步，避免"口型对不上"的问题。
- **高效渲染**：使用 OpenGL 3.3 Core Profile 直接渲染 YUV420P 帧，避免 CPU 端色彩空间转换，降低 GPU 带宽开销。
- **可扩展架构**：解复用、解码、输出三大模块独立运行于各自线程，通过队列解耦，便于扩展新编解码格式和渲染后端。

## 核心特性

- 支持常见媒体封装格式（MP4、MKV、AVI、FLV 等）和编解码器（H.264/H.265/AAC/MP3 等）
- 播放控制：播放 / 暂停 / 停止 / 进度跳转 / 快进快退
- 音量调节、静音切换
- 变速播放（倍速播放）
- 拖拽文件打开 / 命令行参数打开
- 全屏窗口自适应，控制栏自动隐藏（鼠标不动时消失）
- 播放状态回调（加载中、播放中、暂停、跳转中、错误、播放结束）

## 技术栈

| 模块       | 技术选型                                     |
| ---------- | -------------------------------------------- |
| UI 框架    | Qt 6 (Widgets, OpenGLWidgets)                |
| 音视频解码 | FFmpeg 62.x (libavcodec / libavformat / ...) |
| 视频渲染   | OpenGL 3.3 Core Profile (GLSL 着色器)        |
| 音频输出   | Qt Multimedia                                |
| 构建系统   | CMake 3.20+                                  |
| 编译器     | MSVC 2022 (x64)                              |

## 项目架构

```
┌──────────┐    PacketQueue    ┌──────────────┐    FrameQueue    ┌──────────────┐
│          │ ────────────────→ │              │ ───────────────→ │              │
│ Demuxer  │                   │ AudioDecoder │                  │ AudioOutput  │
│ (线程1)  │                   │ (线程2)      │                  │ (Qt 音频后端) │
└──────────┘                   └──────────────┘                  └──────┬───────┘
     │                                                                 │
     │ PacketQueue    ┌──────────────┐    FrameQueue    ┌──────────────┤ 音频时钟
     └──────────────→ │              │ ───────────────→ │              │ 同步信号
                      │ VideoDecoder │                  │ VideoRenderer│←───────────
                      │ (线程3)      │                  │ (OpenGL 渲染)│
                      └──────────────┘                  └──────────────┘
```

- **PacketQueue / FrameQueue**：线程安全的无锁队列，限制最大内存/帧数，防止内存溢出。
- **Clock**：基于音频播放进度的同步时钟，视频帧根据与时钟的偏差决定渲染时机或丢弃。
- **Demuxer**：负责解封装，读取音视频 packet 分别送入对应队列。
- **AudioOutput**：将 PCM 帧送入 Qt 音频后端播放，同时驱动音频时钟。
- **VideoRenderer**：将解码后的 YUV420P 帧通过 OpenGL 着色器渲染到 `QOpenGLWidget`。

## 目录结构

```
multi-mediaPlayer/
├── CMakeLists.txt              # 顶层 CMake 配置
├── build.bat                   # 一键构建脚本（Windows）
├── src/
│   ├── CMakeLists.txt          # 源码 CMake 配置
│   ├── main.cpp                # 入口函数
│   ├── core/                   # 核心播放引擎
│   │   ├── Types.h             # 公共类型定义
│   │   ├── PacketQueue.h/cpp   # 数据包队列
│   │   ├── FrameQueue.h/cpp    # 解码帧队列
│   │   ├── Clock.h/cpp         # 音视频同步时钟
│   │   ├── Demuxer.h/cpp       # 解封装模块
│   │   ├── AudioDecoder.h/cpp  # 音频解码线程
│   │   ├── VideoDecoder.h/cpp  # 视频解码线程
│   │   ├── AudioOutput.h/cpp   # 音频输出后端
│   │   ├── VideoRenderer.h/cpp # OpenGL 视频渲染器
│   │   └── PlayerCore.h/cpp    # 播放状态机 & 核心调度
│   ├── ui/                     # 用户界面
│   │   ├── MainWindow.h/cpp    # 主窗口
│   │   ├── VideoWidget.h/cpp   # 视频渲染控件
│   │   └── ControlBar.h/cpp    # 播放控制栏
│   └── shaders/                # GLSL 着色器
│       ├── yuv.vert            # 顶点着色器
│       └── yuv420p.frag        # YUV420P → RGB 片段着色器
└── resources/
    └── resources.qrc           # Qt 资源文件
```

## 构建与使用

### 依赖安装

在开始构建之前，需要安装以下依赖：

| 依赖            | 推荐版本                     | 说明                     |
| --------------- | ---------------------------- | ------------------------ |
| Qt 6            | 6.8.0+ (MSVC 2022 64-bit)    | 需包含 Multimedia 模块   |
| FFmpeg          | 7.x / 62.x (shared build)    | 需 .dll + .lib + 头文件 |
| Visual Studio   | 2022 (Community 或以上)      | 需安装 C++ 桌面开发负载  |
| CMake           | 3.20+                        |                          |

### 快速构建（Windows）

1. **确认依赖路径**

   编辑 `build.bat`，按你的实际安装路径修改以下变量：

   ```batch
   set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
   set "QT_PATH=E:\DevTool\QT\6.11.0\msvc2022_64"
   set "FFMPEG_PATH=D:\Tools\ffmpeg-shared"
   ```

2. **运行构建脚本**

   ```cmd
   build.bat
   ```

   脚本会依次执行：
   - 配置 MSVC 编译环境
   - 运行 CMake 生成 VS 解决方案
   - 编译 Release 版本
   - 调用 `windeployqt` 复制 Qt 运行时依赖
   - 复制 FFmpeg DLL 到输出目录

3. **运行**

   ```cmd
   .\build\src\Release\MediaPlayer.exe
   ```

   也可通过命令行直接打开文件：

   ```cmd
   .\build\src\Release\MediaPlayer.exe "D:\Videos\sample.mp4"
   ```

### 手动构建

```cmd
# 配置
cmake -S . -B build ^
    -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH="<你的 QT 路径>" ^
    -DFFMPEG_ROOT="<你的 FFmpeg 路径>"

# 编译
cmake --build build --config Release
```

### FFmpeg 目录结构要求

```
D:\Tools\ffmpeg-shared\
├── include/
│   ├── libavcodec/
│   ├── libavformat/
│   ├── libavutil/
│   ├── libswresample/
│   ├── libswscale/
│   └── libavfilter/
├── bin/
│   ├── avcodec-62.lib       # MSVC 导入库
│   ├── avformat-62.lib
│   ├── ...
│   ├── avcodec-62.dll       # 运行时 DLL
│   ├── avformat-62.dll
│   └── ...
└── lib/                      # 备选 .lib 路径
```

## 使用说明

| 操作           | 方式                                                |
| -------------- | --------------------------------------------------- |
| 打开文件       | 拖拽媒体文件到窗口 / 命令行参数 / 菜单栏              |
| 播放 / 暂停    | 空格键 / 点击控制栏按钮                              |
| 快进 / 快退    | ← → 方向键（±5 秒）/ 拖动进度条                      |
| 音量调节       | 鼠标滚轮 / 控制栏音量滑块                            |
| 静音切换       | M 键                                                |
| 全屏切换       | F 键 / 双击视频区域                                  |
| 显示控制栏     | 移动鼠标到窗口底部                                   |

## License

MIT License
