#include "timer.h"


TwinStudio_Timer TwinStudio_CreateTimer(float startTime, bool isRestarting, TwinStudio_TimerCallback callback)
{
    return (TwinStudio_Timer) {
        .startTime = startTime,
        .isRestarting = isRestarting,
        .stopCallback = callback,
        .currentClock = 0,
        .hasFired = false
    };
}


void TwinStudio_UpdateTimer(TwinStudio_Timer* timer, float dt)
{
    if (timer->hasFired && !timer->isRestarting)
    {
        return;
    }

    timer->currentClock += dt;
    if (timer->currentClock >= timer->startTime)
    {
        if (timer->stopCallback)
        {
            timer->stopCallback();
        }

        timer->currentClock = 0;
        timer->hasFired = true;
    }
}