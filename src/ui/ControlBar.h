#pragma once
#include <QWidget>
#include <QFrame>

class QPushButton;
class QSlider;
class QLabel;
class QComboBox;
class QFrame;

class ControlBar : public QWidget {
    Q_OBJECT
public:
    explicit ControlBar(QWidget* parent = nullptr);

    void setDuration(double sec);
    void setPosition(double sec);
    void setVolume(double vol);
    void setPlaying(bool playing);
    void setMuted(bool muted);
    bool isDraggingSeek() const { return sliderDragging_; }
    bool cursorOverVolumeControls() const;
    void hideVolumePopup() { if (volumePopup_) volumePopup_->hide(); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void playPauseClicked();
    void stopClicked();
    void seekRequested(double seconds);
    void volumeChanged(double volume);
    void muteToggled();
    void playbackRateChanged(double rate);
    void interactionStarted();
    void interactionEnded();

private:
    void setupUi();
    QString formatTime(double sec) const;
    void updateTimeText();
    void updateSeekSliderFromMouse(const QPoint& pos);
    void finishSeekInteraction();
    void showVolumePopup();
    void hideVolumePopupIfNeeded();

    QPushButton* playPauseBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QPushButton* muteBtn_ = nullptr;
    QSlider* seekSlider_ = nullptr;
    QSlider* volumeSlider_ = nullptr;
    QComboBox* speedBox_ = nullptr;
    QFrame* volumePopup_ = nullptr;
    QLabel* volumeValueLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QLabel* durationLabel_ = nullptr;

    bool isPlaying_ = false;
    bool isMuted_ = false;
    int volumeBeforeMute_ = 80;
    double duration_ = 0.0;
    double position_ = 0.0;
    bool sliderDragging_ = false;
};
