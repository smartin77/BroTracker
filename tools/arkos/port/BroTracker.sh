#!/bin/bash

launcher_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
app_dir="$launcher_dir/brotracker"

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
    controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
    controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
    controlfolder="$XDG_DATA_HOME/PortMaster"
else
    controlfolder="/roms/ports/PortMaster"
fi

if [ -f "$controlfolder/control.txt" ]; then
    source "$controlfolder/control.txt"
    get_controls
    export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
fi

cd -- "$app_dir" || exit 1

if type pm_platform_helper >/dev/null 2>&1; then
    pm_platform_helper "$app_dir/BroTrackerArkOSUI"
fi

./BroTrackerArkOSUI
EXIT_CODE=$?

if type pm_finish >/dev/null 2>&1; then
    pm_finish
fi

exit $EXIT_CODE
