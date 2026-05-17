#include "ControlBar.h"
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>
#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QEvent>
#include <QMouseEvent>
#include <QCursor>
#include <QTimer>
#include <cmath>
#include <cstdio>

namespace {
enum class PlayerIcon {
    Play,
    Pause,
    Stop,
    Speed,
    Volume,
    Muted
};

QIcon makePlayerIcon(PlayerIcon icon, QColor color = Qt::white)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    switch (icon) {
    case PlayerIcon::Play: {
        QPolygonF triangle;
        triangle << QPointF(11, 8) << QPointF(24, 16) << QPointF(11, 24);
        painter.drawPolygon(triangle);
        break;
    }
    case PlayerIcon::Pause:
        painter.drawRoundedRect(QRectF(9, 8, 5, 16), 1.2, 1.2);
        painter.drawRoundedRect(QRectF(18, 8, 5, 16), 1.2, 1.2);
        break;
    case PlayerIcon::Stop:
        painter.drawRoundedRect(QRectF(10, 10, 12, 12), 1.5, 1.5);
        break;
    case PlayerIcon::Speed:
        break;
    case PlayerIcon::Volume: {
        QPolygonF speaker;
        speaker << QPointF(7, 13) << QPointF(12, 13) << QPointF(18, 8)
                << QPointF(18, 24) << QPointF(12, 19) << QPointF(7, 19);
        painter.drawPolygon(speaker);
        QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(16, 10, 8, 12), -45 * 16, 90 * 16);
        painter.drawArc(QRectF(14, 7, 14, 18), -45 * 16, 90 * 16);
        break;
    }
    case PlayerIcon::Muted: {
        QPolygonF speaker;
        speaker << QPointF(7, 13) << QPointF(12, 13) << QPointF(18, 8)
                << QPointF(18, 24) << QPointF(12, 19) << QPointF(7, 19);
        painter.drawPolygon(speaker);
        QPen pen(color, 2.2, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(QPointF(22, 12), QPointF(28, 20));
        painter.drawLine(QPointF(28, 12), QPointF(22, 20));
        break;
    }
    }

    return QIcon(pixmap);
}
}

ControlBar::ControlBar(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void ControlBar::setupUi()
{
    setAutoFillBackground(true);
    setMouseTracking(true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 9, 16, 10);
    rootLayout->setSpacing(7);

    playPauseBtn_ = new QPushButton(this);
    playPauseBtn_->setIcon(makePlayerIcon(PlayerIcon::Play));
    playPauseBtn_->setIconSize(QSize(28, 28));
    playPauseBtn_->setFixedSize(46, 40);
    playPauseBtn_->setFocusPolicy(Qt::NoFocus);
    connect(playPauseBtn_, &QPushButton::clicked, this, &ControlBar::playPauseClicked);

    stopBtn_ = new QPushButton(this);
    stopBtn_->setIcon(makePlayerIcon(PlayerIcon::Stop, QColor(235, 235, 235)));
    stopBtn_->setIconSize(QSize(24, 24));
    stopBtn_->setFixedSize(42, 40);
    stopBtn_->setFocusPolicy(Qt::NoFocus);
    connect(stopBtn_, &QPushButton::clicked, this, &ControlBar::stopClicked);

    timeLabel_ = new QLabel("00:00", this);
    timeLabel_->setStyleSheet("color: rgba(255, 255, 255, 0.85); font-size: 13px; font-weight: 500; "
                              "font-family: \"Segoe UI\", \"Microsoft YaHei\", sans-serif;");
    timeLabel_->setFixedHeight(40);
    timeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    durationLabel_ = new QLabel("/ 00:00", this);
    durationLabel_->setStyleSheet("color: rgba(255, 255, 255, 0.50); font-size: 13px; "
                                  "font-family: \"Segoe UI\", \"Microsoft YaHei\", sans-serif;");
    durationLabel_->setFixedHeight(40);
    durationLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    seekSlider_ = new QSlider(Qt::Horizontal, this);
    seekSlider_->setObjectName(QStringLiteral("SeekSlider"));
    seekSlider_->setRange(0, 10000);
    seekSlider_->setValue(0);
    seekSlider_->setTracking(false);
    seekSlider_->setFocusPolicy(Qt::NoFocus);
    seekSlider_->setFixedHeight(12);
    seekSlider_->installEventFilter(this);
    seekSlider_->setCursor(Qt::PointingHandCursor);
    connect(seekSlider_, &QSlider::sliderPressed, this, [this] {
        sliderDragging_ = true;
        emit interactionStarted();
    });
    connect(seekSlider_, &QSlider::sliderMoved, this, [this](int value) {
        if (!sliderDragging_) return;
        double fraction = value / 10000.0;
        position_ = fraction * duration_;
        updateTimeText();
    });
    connect(seekSlider_, &QSlider::sliderReleased, this, [this] {
        finishSeekInteraction();
    });

    muteBtn_ = new QPushButton(this);
    muteBtn_->setIcon(makePlayerIcon(PlayerIcon::Volume));
    muteBtn_->setIconSize(QSize(26, 26));
    muteBtn_->setFixedSize(42, 40);
    muteBtn_->setFocusPolicy(Qt::NoFocus);
    muteBtn_->installEventFilter(this);
    connect(muteBtn_, &QPushButton::clicked, this, [this] {
        emit muteToggled();
        showVolumePopup();
    });

    volumePopup_ = new QFrame(this, Qt::ToolTip | Qt::FramelessWindowHint);
    volumePopup_->setObjectName(QStringLiteral("VolumePopup"));
    volumePopup_->setAttribute(Qt::WA_ShowWithoutActivating);
    volumePopup_->setFixedSize(46, 118);
    volumePopup_->hide();
    volumePopup_->installEventFilter(this);

    auto* volumeLayout = new QVBoxLayout(volumePopup_);
    volumeLayout->setContentsMargins(8, 6, 8, 6);
    volumeLayout->setSpacing(6);

    volumeValueLabel_ = new QLabel("80", volumePopup_);
    volumeValueLabel_->setObjectName(QStringLiteral("VolumeValue"));
    volumeValueLabel_->setAlignment(Qt::AlignCenter);
    volumeValueLabel_->installEventFilter(this);

    volumeSlider_ = new QSlider(Qt::Vertical, volumePopup_);
    volumeSlider_->setObjectName(QStringLiteral("VolumeSlider"));
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(80);
    // volumeSlider_->setInvertedAppearance(true);
    volumeSlider_->setFixedSize(24, 74);
    volumeSlider_->setFocusPolicy(Qt::NoFocus);
    volumeSlider_->setCursor(Qt::PointingHandCursor);
    volumeSlider_->installEventFilter(this);
    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int v) {
        volumeValueLabel_->setText(QString::number(v));
        emit volumeChanged(v / 100.0);
    });
    volumeLayout->addStretch();
    volumeLayout->addWidget(volumeValueLabel_);
    volumeLayout->addWidget(volumeSlider_, 0, Qt::AlignHCenter);
    volumeLayout->addStretch();

    speedBox_ = new QComboBox(this);
    speedBox_->setFocusPolicy(Qt::NoFocus);
    speedBox_->setCursor(Qt::PointingHandCursor);
    speedBox_->addItem(QStringLiteral("0.5x"), 0.5);
    speedBox_->addItem(QStringLiteral("0.75x"), 0.75);
    speedBox_->addItem(QStringLiteral("1.0x"), 1.0);
    speedBox_->addItem(QStringLiteral("1.25x"), 1.25);
    speedBox_->addItem(QStringLiteral("1.5x"), 1.5);
    speedBox_->addItem(QStringLiteral("2.0x"), 2.0);
    speedBox_->setCurrentIndex(2);
    speedBox_->setFixedSize(70, 40);
    connect(speedBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        emit playbackRateChanged(speedBox_->itemData(index).toDouble());
        emit interactionEnded();
    });

    auto* controlLayout = new QHBoxLayout();
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(8);

    controlLayout->addWidget(playPauseBtn_, 0, Qt::AlignVCenter);
    controlLayout->addWidget(stopBtn_, 0, Qt::AlignVCenter);
    controlLayout->addWidget(timeLabel_, 0, Qt::AlignVCenter);
    controlLayout->addWidget(durationLabel_, 0, Qt::AlignVCenter);
    controlLayout->addStretch(1);
    controlLayout->addWidget(speedBox_, 0, Qt::AlignVCenter);
    controlLayout->addWidget(muteBtn_, 0, Qt::AlignVCenter);

    rootLayout->addWidget(seekSlider_);
    rootLayout->addLayout(controlLayout);

    setStyleSheet(R"(
        ControlBar {
            background: qlineargradient(
                x1: 0, y1: 0, x2: 0, y2: 1,
                stop: 0 rgba(24, 24, 24, 226),
                stop: 1 rgba(16, 16, 16, 238)
            );
            border-top: 1px solid rgba(255, 255, 255, 0.10);
        }
        QPushButton, QComboBox {
            background: transparent;
            border: none;
            color: white;
            font-size: 13px;
            border-radius: 3px;
        }
        QPushButton:hover, QComboBox:hover {
            background-color: rgba(255, 255, 255, 0.12);
        }
        QPushButton:pressed {
            background-color: rgba(255, 102, 153, 0.24);
        }
        QComboBox {
            color: rgba(255, 255, 255, 0.82);
            padding-left: 9px;
            padding-right: 9px;
        }
        QComboBox::drop-down {
            border: none;
            width: 0px;
        }
        QComboBox QAbstractItemView {
            color: white;
            background: rgb(35, 35, 35);
            selection-background-color: #ff6699;
            outline: none;
            border: 1px solid rgba(255, 255, 255, 0.14);
        }
        QLabel {
            background: transparent;
        }
        /* --- Seek slider --- */
        QSlider#SeekSlider::groove:horizontal {
            border: none;
            height: 2px;
            background: transparent;
        }
        QSlider#SeekSlider::handle:horizontal {
            background: #00aeec;
            border: none;
            width: 10px;
            height: 10px;
            margin: -4px 0;
            border-radius: 5px;
        }
        QSlider#SeekSlider::add-page:horizontal {
            background: rgba(255, 255, 255, 0.18);
            border-radius: 1px;
        }
        QSlider#SeekSlider::sub-page:horizontal {
            background: #00aeec;
            border-radius: 1px;
        }
        QSlider#SeekSlider:hover::handle:horizontal {
            width: 12px;
            height: 12px;
            margin: -5px 0;
            border-radius: 6px;
        }
        QFrame#VolumePopup {
            background: rgba(31, 31, 31, 238);
            border: 1px solid rgba(255, 255, 255, 0.08);
            border-radius: 4px;
        }
        QLabel#VolumeValue {
            color: rgba(255, 255, 255, 0.86);
            font-size: 13px;
            font-weight: 500;
        }
        /* --- Volume slider --- */
        QSlider#VolumeSlider::groove:vertical {
            width: 2px;
            background: transparent;
            border-radius: 1px;
        }
        QSlider#VolumeSlider::sub-page:vertical {
            background: rgba(255, 255, 255, 0.18);
            border-radius: 1px;
        }
        QSlider#VolumeSlider::add-page:vertical {
            background: #00aeec;
            border-radius: 1px;
        }
        QSlider#VolumeSlider::handle:vertical {
            background: #00aeec;
            border: none;
            width: 10px;
            height: 10px;
            margin: 0 -4px;
            border-radius: 5px;
        }
    )");
}

bool ControlBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == muteBtn_ || watched == volumePopup_ ||
        watched == volumeSlider_ || watched == volumeValueLabel_) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::MouseButtonPress) {
            showVolumePopup();
        } else if (event->type() == QEvent::Leave) {
            QTimer::singleShot(130, this, &ControlBar::hideVolumePopupIfNeeded);
        } else if (event->type() == QEvent::Wheel) {
            auto* we = static_cast<QWheelEvent*>(event);
            int delta = we->angleDelta().y();
            int v = volumeSlider_->value() + (delta > 0 ? 2 : -2);
            v = std::clamp(v, 0, 100);
            volumeSlider_->setValue(v);
            if (isMuted_ && v > 0) {
                volumeBeforeMute_ = v;
                emit muteToggled();
            }
            return true;
        }
    }

    if (watched == seekSlider_) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                sliderDragging_ = true;
                seekSlider_->setSliderDown(true);
                updateSeekSliderFromMouse(mouseEvent->pos());
                emit interactionStarted();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            if (sliderDragging_) {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                updateSeekSliderFromMouse(mouseEvent->pos());
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && sliderDragging_) {
                updateSeekSliderFromMouse(mouseEvent->pos());
                seekSlider_->setSliderDown(false);
                finishSeekInteraction();
                return true;
            }
            break;
        }
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void ControlBar::updateSeekSliderFromMouse(const QPoint& pos)
{
    int width = std::max(1, seekSlider_->width());
    double fraction = std::clamp(pos.x() / static_cast<double>(width), 0.0, 1.0);
    int value = static_cast<int>(fraction * seekSlider_->maximum());
    seekSlider_->setValue(value);
    position_ = fraction * duration_;
    updateTimeText();
}

void ControlBar::finishSeekInteraction()
{
    if (!sliderDragging_)
        return;

    sliderDragging_ = false;
    double fraction = seekSlider_->value() / 10000.0;
    emit seekRequested(fraction * duration_);
    emit interactionEnded();
}

void ControlBar::setDuration(double sec)
{
    duration_ = sec;
    updateTimeText();
}

void ControlBar::setPosition(double sec)
{
    if (sliderDragging_)
        return;
    position_ = sec;
    int value = duration_ > 0 ? static_cast<int>(sec / duration_ * 10000) : 0;
    seekSlider_->setValue(std::clamp(value, 0, 10000));
    updateTimeText();
}

void ControlBar::setVolume(double vol)
{
    volumeSlider_->blockSignals(true);
    int value = std::clamp(static_cast<int>(std::llround(vol * 100)), 0, 100);
    volumeSlider_->setValue(value);
    volumeSlider_->blockSignals(false);
    volumeValueLabel_->setText(QString::number(value));
}

void ControlBar::setPlaying(bool playing)
{
    isPlaying_ = playing;
    playPauseBtn_->setIcon(makePlayerIcon(playing ? PlayerIcon::Pause : PlayerIcon::Play));
}

void ControlBar::setMuted(bool muted)
{
    if (isMuted_ == muted) return;
    isMuted_ = muted;
    muteBtn_->setIcon(makePlayerIcon(muted ? PlayerIcon::Muted : PlayerIcon::Volume));

    volumeSlider_->blockSignals(true);
    if (muted) {
        volumeBeforeMute_ = volumeSlider_->value();
        volumeSlider_->setValue(0);
        volumeValueLabel_->setText(QStringLiteral("0"));
    } else {
        volumeSlider_->setValue(volumeBeforeMute_);
        volumeValueLabel_->setText(QString::number(volumeBeforeMute_));
    }
    volumeSlider_->blockSignals(false);
}

void ControlBar::showVolumePopup()
{
    if (!volumePopup_ || !muteBtn_)
        return;

    QPoint btnGlobal = muteBtn_->mapToGlobal(QPoint(0, 0));
    int x = btnGlobal.x() + muteBtn_->width() / 2 - volumePopup_->width() / 2;
    int y = btnGlobal.y() - volumePopup_->height() - 8;
    volumePopup_->move(x, y);
    volumePopup_->show();
    volumePopup_->raise();
}

void ControlBar::hideVolumePopupIfNeeded()
{
    if (volumePopup_ && !cursorOverVolumeControls())
        volumePopup_->hide();
}

bool ControlBar::cursorOverVolumeControls() const
{
    if (!muteBtn_ || !volumePopup_)
        return false;

    QPoint globalPos = QCursor::pos();
    return muteBtn_->rect().contains(muteBtn_->mapFromGlobal(globalPos)) ||
        volumePopup_->isVisible() &&
        volumePopup_->rect().contains(volumePopup_->mapFromGlobal(globalPos));
}

void ControlBar::updateTimeText()
{
    timeLabel_->setText(formatTime(position_));
    durationLabel_->setText(QStringLiteral("/ ") + formatTime(duration_));
}

QString ControlBar::formatTime(double sec) const
{
    if (sec < 0) sec = 0;
    int seconds = static_cast<int>(std::llround(sec));
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;

    char buf[16];
    if (h > 0)
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    else
        std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return QString::fromLatin1(buf);
}
