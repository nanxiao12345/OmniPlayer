set(CORE_SOURCES
    main.cpp
    core/PacketQueue.cpp
    core/FrameQueue.cpp
    core/Clock.cpp
    core/Demuxer.cpp
    core/AudioDecoder.cpp
    core/VideoDecoder.cpp
    core/AudioOutput.cpp
    core/VideoRenderer.cpp
    core/PlayerCore.cpp
)

set(CORE_HEADERS
    core/Types.h
    core/PacketQueue.h
    core/FrameQueue.h
    core/Clock.h
    core/Demuxer.h
    core/AudioDecoder.h
    core/VideoDecoder.h
    core/AudioOutput.h
    core/VideoRenderer.h
    core/PlayerCore.h
)

set(UI_SOURCES
    ui/MainWindow.cpp
    ui/VideoWidget.cpp
    ui/ControlBar.cpp
)

set(UI_HEADERS
    ui/MainWindow.h
    ui/VideoWidget.h
    ui/ControlBar.h
)

qt_add_resources(RESOURCE_FILES ../resources/resources.qrc)

add_executable(MediaPlayer
    ${CORE_SOURCES} ${CORE_HEADERS}
    ${UI_SOURCES} ${UI_HEADERS}
    ${RESOURCE_FILES}
)

target_include_directories(MediaPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(MediaPlayer PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::OpenGLWidgets
    OpenGL::GL
    ${FFMPEG_LIBRARIES}
)

file(GLOB FFMPEG_RUNTIME_DLLS
    "${FFMPEG_ROOT}/bin/av*.dll"
    "${FFMPEG_ROOT}/bin/sw*.dll"
)
foreach(runtime_dll ${FFMPEG_RUNTIME_DLLS})
    add_custom_command(TARGET MediaPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${runtime_dll}"
            "$<TARGET_FILE_DIR:MediaPlayer>"
    )
endforeach()
