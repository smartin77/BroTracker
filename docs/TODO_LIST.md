# TODO List

This document contains deferred implementation, cleanup and refactoring tasks that should be remembered but do not belong to the project milestones.

It does not replace the project roadmap or milestone planning.

Items may be added during development when a task is identified but is not important enough to interrupt the current development step.

## Deferred Tasks

### Audio Diagnostics

* [ ] Move the current `AudioTestSource` test path from `firmware/teensy/BroTracker/` to `tools/teensy_diagnostics/`.

  * Keep it available as a Teensy audio hardware verification tool.
  * Keep production BroTracker runtime code separate from diagnostic/test-only code.
  * Update the build/integration path and documentation as necessary.
