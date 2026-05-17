#pragma once
#include <mutex>

class Clock {
public:
    Clock();

    void reset();
    void setTime(double pts);
    double getTime() const;

    void pause();
    void resume();

    void setSpeed(double speed);
    double speed() const { return speed_; }

private:
    static double systemTimeSecs();

    double ptsDrift_ = 0.0;
    double lastPts_ = 0.0;
    double lastSetSystemTime_ = 0.0;
    double pausedAccum_ = 0.0;
    bool paused_ = true;
    double speed_ = 1.0;
    mutable std::mutex mutex_;
};
