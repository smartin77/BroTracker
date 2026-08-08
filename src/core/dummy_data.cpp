#include "dummy_data.h"

Tune CreateDummyTune()
{
    Tune tune;

    tune.pattern.number = 5;

    tune.pattern.channels.resize(8);

    for (auto& channel : tune.pattern.channels)
    {
        channel.rows.resize(16);
    }

    return tune;
}