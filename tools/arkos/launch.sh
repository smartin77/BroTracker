#!/usr/bin/env bash
set -e

cd -- "$(dirname -- "$0")"
# XDG serves diagnostic evidence only; runtime config is beside the executable.
export XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
exec ./BroTrackerTerminal "$@"
