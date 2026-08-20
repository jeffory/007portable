#!/bin/bash
# PortMaster launcher for the GoldenEye 007 PC port.
#
# Layout inside the PortMaster zip:
#   GE007.sh            this launcher
#   ge007/ge007-port    aarch64 binary (SDL2/GLES via the system libs)
#   ge007/README.md     ROM placement instructions
#   ge007/licenses/
#
# The user must supply the US ROM (not distributed):
#   ge007/data/ge007.u.z64   (z64 byte order,
#                             sha1 abe01e4aeb033b6c0836819f549c791b26cfde83)
#
# UNTESTED ON REAL HARDWARE YET — assembled per the PortMaster conventions
# (see https://portmaster.games/porting.html); expect to iterate on the
# control setup and GL flags per device.

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

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/ge007"
cd "$GAMEDIR"

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export XDG_CONFIG_HOME="$GAMEDIR/conf"
mkdir -p "$XDG_CONFIG_HOME"

# native SDL_GameController handles the pad; no gptokeyb needed
export PORT_MOUSELOOK=0        # no mouse on handhelds: stock 1.1 controls
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

$GPTOKEYB &  # still start it so the hotkey combo can quit the port
./ge007-port
$ESUDO kill -9 $(pidof gptokeyb) 2>/dev/null
printf "\033c" > /dev/tty0
