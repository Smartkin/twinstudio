#ifndef TS_TIMER_H
#define TS_TIMER_H

#include <stdbool.h>

typedef void (*TwinStudio_TimerCallback)();


typedef struct TwinStudio_Timer {
    float startTime; // In seconds
    float currentClock; // In seconds
    bool isRestarting;
    bool hasFired;
    TwinStudio_TimerCallback stopCallback;
} TwinStudio_Timer;


TwinStudio_Timer TwinStudio_CreateTimer(float startTime, bool isRestarting, TwinStudio_TimerCallback callback);
void TwinStudio_UpdateTimer(TwinStudio_Timer* timer, float dt);


#endif