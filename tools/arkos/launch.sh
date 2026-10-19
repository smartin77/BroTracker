#!/usr/bin/env bash
set -e

cd -- "$(dirname -- "$0")"
export XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
exec ./BroTrackerTerminal "$@"
