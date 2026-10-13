# Captured STOP evidence

Original current/numbered logs and config were copied unchanged before tests.
manifest.json identifies source revision and initial status. The extracted sequence
is derived evidence; originals remain preserved. No BTX launch, port access or
firmware upload occurred for this correction. An asynchronous worker fixture
reproduced payload replacement while Write returned zero, missing STOP transmission
and partial framing corruption. Full/partial completion and recovery tests validate
the host fix; Windows + Teensy retest remains pending.
