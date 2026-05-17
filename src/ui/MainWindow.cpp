#include "MainWindow.h"
#include "VideoWidget.h"
#include "ControlBar.h"
#include "../core/PlayerCore.h"
#include "../core/Types.h"

#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QShowEvent>
#include <QEvent>
#include <QCursor>
#include <QUrl>
#include <QFileDialog>
#include <QMessageBox>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setWindowTitle("MediaPlayer");

    playerCore_ = new PlayerCore(this);
    setupUi();
    setupConnections();

    hideControlsTimer_ = new QTimer(this);
    hideControlsTimer_->setSingleShot(true);
    hideControlsTimer_->setInterval(2600);
    connect(hideControlsTimer_, &QTimer::timeout, this, [this] {
        if (!shouldKeepControlBarVisible())
            showControlBar(false);
        else
            scheduleControlBarHide();
    });

    setFocusPolicy(Qt::StrongFocus);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    auto* central = new QWidget(this);
    central->setMouseTracking(true);
    setCentralWidget(central);

    videoWidget_ = new VideoWidget(central);
    videoWidget_->setMouseTracking(true);
    videoWidget_->setFrameQueue(&playerCore_->videoFrameQueue());

    controlBar_ = new ControlBar(central);
    controlBar_->setFixedHeight(76);
    controlBar_->setMouseTracking(true);
    controlBar_->raise();

    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(videoWidget_);

    central->installEventFilter(this);
    videoWidget_->installEventFilter(this);
    controlBar_->installEventFilter(this);
    for (auto* child : controlBar_->findChildren<QWidget*>()) {
        child->setMouseTracking(true);
        child->installEventFilter(this);
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateControlBarPosition();
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    updateControlBarPosition();
}

void MainWindow::updateControlBarPosition()
{
    if (!controlBar_) return;
    QWidget* central = centralWidget();
    if (!central) return;
    QRect geo = central->rect();
    int barH = 76;
    int margin = 0;
    controlBar_->setGeometry(
        margin,
        geo.height() - barH,
        geo.width() - 2 * margin,
        barH
    );
}

void MainWindow::setupConnections()
{
    connect(playerCore_, &PlayerCore::stateChanged, this, &MainWindow::onStateChanged);
    connect(playerCore_, &PlayerCore::timeChanged, this, &MainWindow::onPositionChanged);
    connect(playerCore_, &PlayerCore::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(playerCore_, &PlayerCore::fileLoaded, this, [this] {
        videoWidget_->setAudioClock(&playerCore_->audioClock());
        videoWidget_->setSyncToAudio(playerCore_->hasAudio());
        revealControlBar();
    });
    connect(playerCore_, &PlayerCore::playbackFinished, this, &MainWindow::onPlaybackFinished);
    connect(playerCore_, &PlayerCore::seekStarted, videoWidget_, &VideoWidget::beginSeek);
    connect(videoWidget_, &VideoWidget::seekFrameReady, this, [this] {
        if (playerCore_->state() == PlayerState::Paused)
            videoWidget_->stopRendering();
    });

    connect(controlBar_, &ControlBar::playPauseClicked, playerCore_, &PlayerCore::togglePlayPause);
    connect(controlBar_, &ControlBar::stopClicked, playerCore_, &PlayerCore::stop);
    connect(controlBar_, &ControlBar::seekRequested, playerCore_, &PlayerCore::seek);
    connect(controlBar_, &ControlBar::volumeChanged, playerCore_, &PlayerCore::setVolume);
    connect(controlBar_, &ControlBar::playbackRateChanged, playerCore_, &PlayerCore::setPlaybackRate);
    connect(controlBar_, &ControlBar::interactionStarted, this, [this] {
        controlInteractionActive_ = true;
        revealControlBar();
        hideControlsTimer_->stop();
    });
    connect(controlBar_, &ControlBar::interactionEnded, this, [this] {
        controlInteractionActive_ = false;
        scheduleControlBarHide();
    });
    connect(controlBar_, &ControlBar::muteToggled, this, [this] {
        static bool muted = false;
        muted = !muted;
        playerCore_->setMuted(muted);
        controlBar_->setMuted(muted);
    });
}

void MainWindow::onStateChanged(PlayerState newState)
{
    switch (newState) {
    case PlayerState::Playing:
        controlBar_->setPlaying(true);
        videoWidget_->startRendering();
        revealControlBar();
        scheduleControlBarHide();
        break;
    case PlayerState::Paused:
        controlBar_->setPlaying(false);
        if (!videoWidget_->isSeekFramePending())
            videoWidget_->stopRendering();
        revealControlBar();
        break;
    case PlayerState::Seeking:
        videoWidget_->startRendering();
        break;
    case PlayerState::Stopped:
        controlBar_->setPlaying(false);
        videoWidget_->stopRendering();
        videoWidget_->clearFrame();
        revealControlBar();
        break;
    case PlayerState::Error:
        controlBar_->setPlaying(false);
        videoWidget_->stopRendering();
        videoWidget_->clearFrame();
        break;
    default:
        break;
    }
}

void MainWindow::onPositionChanged(double current, double total)
{
    controlBar_->setDuration(total);
    controlBar_->setPosition(current);
}

void MainWindow::onErrorOccurred(const QString& msg)
{
    QMessageBox::warning(this, "Error", msg);
}

void MainWindow::onPlaybackFinished()
{
    playerCore_->stop();
}

void MainWindow::openFile(const QString& filePath)
{
    QUrl url(filePath);
    QString localFile = url.isLocalFile() ? url.toLocalFile() : filePath;
    if (!playerCore_->openFile(localFile))
        return;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile()) {
                openFile(url.toLocalFile());
                break;
            }
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        playerCore_->togglePlayPause();
        break;
    case Qt::Key_Left:
        playerCore_->seekRelative(-5.0);
        break;
    case Qt::Key_Right:
        playerCore_->seekRelative(5.0);
        break;
    case Qt::Key_O:
        if (event->modifiers() & Qt::ControlModifier) {
            QString file = QFileDialog::getOpenFileName(this,
                "Open Media File", QString(),
                "Media Files (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.mp3 *.wav *.flac);;All Files (*.*)");
            if (!file.isEmpty())
                openFile(file);
        }
        break;
    default:
        QMainWindow::keyPressEvent(event);
        break;
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent* event)
{
    QMainWindow::mouseMoveEvent(event);
    revealControlBar();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == videoWidget_ && event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            revealControlBar();
            playerCore_->togglePlayPause();
            return true;
        }
    }

    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::HoverMove:
    case QEvent::Enter:
    case QEvent::MouseButtonPress:
        revealControlBar();
        break;
    case QEvent::MouseButtonRelease:
        revealControlBar();
        scheduleControlBarHide();
        break;
    case QEvent::Leave:
        scheduleControlBarHide();
        break;
    default:
        break;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::showControlBar(bool visible)
{
    controlsVisible_ = visible;
    if (controlBar_) {
        controlBar_->setVisible(visible);
        if (visible)
            controlBar_->raise();
        else
            controlBar_->hideVolumePopup();
    }
}

void MainWindow::revealControlBar()
{
    showControlBar(true);
    scheduleControlBarHide();
}

void MainWindow::scheduleControlBarHide()
{
    if (!hideControlsTimer_ || !controlBar_)
        return;

    if (playerCore_->state() != PlayerState::Playing || shouldKeepControlBarVisible()) {
        hideControlsTimer_->stop();
        showControlBar(true);
        return;
    }

    hideControlsTimer_->start();
}

bool MainWindow::shouldKeepControlBarVisible() const
{
    if (!controlBar_ || !playerCore_)
        return true;
    if (playerCore_->state() != PlayerState::Playing)
        return true;
    if (controlInteractionActive_ || controlBar_->isDraggingSeek())
        return true;
    return cursorOverControlBar();
}

bool MainWindow::cursorOverControlBar() const
{
    if (!controlBar_ || !controlBar_->isVisible())
        return false;

    if (controlBar_->cursorOverVolumeControls())
        return true;

    QPoint localPos = controlBar_->mapFromGlobal(QCursor::pos());
    return controlBar_->rect().contains(localPos);
}
