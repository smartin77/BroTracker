#include "runtime/runtime.h"

int main()
{
    BroTracker::Runtime runtime;

    if (!runtime.Initialize())
        return 1;

    runtime.RunOnce();
    return 0;
}
