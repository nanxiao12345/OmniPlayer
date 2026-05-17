#pragma once
#include <QMainWindow>
#include <QTimer>
#include "../core/Types.h"

class PlayerCore;
class VideoWidget;
class ControlBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void openFile(const QString& filePath);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onStateChanged(PlayerState newState);
    void onPositionChanged(double current, double total);
    void onErrorOccurred(const QString& msg);
    void onPlaybackFinished();

private:
    void setupUi();
    void setupConnections();
    void showControlBar(bool visible);
    void revealControlBar();
    void scheduleControlBarHide();
    bool shouldKeepControlBarVisible() const;
    bool cursorOverControlBar() const;
    void updateControlBarPosition();

    PlayerCore* playerCore_ = nullptr;
    VideoWidget* videoWidget_ = nullptr;
    ControlBar* controlBar_ = nullptr;

    QTimer* hideControlsTimer_ = nullptr;
    bool controlsVisible_ = true;
    bool controlInteractionActive_ = false;
};
