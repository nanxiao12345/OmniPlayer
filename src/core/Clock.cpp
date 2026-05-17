#include "Clock.h"
#include <chrono>

Clock::Clock()
{
    reset();
}

double Clock::systemTimeSecs()
{
    using namespace std::chrono;
    auto now = steady_clock::now().time_since_epoch();
    return duration_cast<duration<double>>(now).count();
}

void Clock::reset()
{
    std::lock_guard lock(mutex_);
    ptsDrift_ = 0.0;
    lastPts_ = 0.0;
    lastSetSystemTime_ = systemTimeSecs();
    pausedAccum_ = 0.0;
    paused_ = true;
}

void Clock::setTime(double pts)
{
    std::lock_guard lock(mutex_);
    double now = systemTimeSecs();
    // Derive drift so that getTime() returns the correct extrapolated PTS
    ptsDrift_ = pts - now;
    lastPts_ = pts;
    lastSetSystemTime_ = now;
    if (paused_) {
        pausedAccum_ = 0.0;
    }
}

double Clock::getTime() const
{
    std::lock_guard lock(mutex_);
    if (paused_)
        return lastPts_;
    double now = systemTimeSecs();
    double elapsed = now - lastSetSystemTime_;
    return lastPts_ + elapsed * speed_;
}

void Clock::pause()
{
    std::lock_guard lock(mutex_);
    if (!paused_) {
        paused_ = true;
    }
}

void Clock::resume()
{
    std::lock_guard lock(mutex_);
    if (paused_) {
        lastSetSystemTime_ = systemTimeSecs();
        paused_ = false;
    }
}

void Clock::setSpeed(double speed)
{
    std::lock_guard lock(mutex_);
    speed_ = speed > 0.0 ? speed : 1.0;
}
