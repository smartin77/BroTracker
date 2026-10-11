# 1 "C:\\Users\\smartin\\AppData\\Local\\Temp\\tmpl2744nt4"
#include <Arduino.h>
# 1 "D:/dev/smartin77/BroTracker/firmware/teensy/BroTracker/BroTracker.ino"
# 10 "D:/dev/smartin77/BroTracker/firmware/teensy/BroTracker/BroTracker.ino"
#include "platform.h"
void setup();
void loop();
#line 12 "D:/dev/smartin77/BroTracker/firmware/teensy/BroTracker/BroTracker.ino"
void setup()
{
    BroTracker::PlatformInit();
    BroTracker::KernelInit();
}

void loop()
{
    BroTracker::KernelRun();
}