#!/bin/bash
# Portable launcher for Stepping Stones. Put this file and the "steppingstones" folder side by side in the
# firmware's ports folder (e.g. /roms/ports or /storage/roms/ports) — nothing else is needed.
# Works on ROCKNIX (Sway/Wayland + PipeWire) and on firmwares without Wayland (KMS/DRM, ALSA).
HERE="$(cd "$(dirname "$0")" && pwd)"
GAMEDIR="$HERE/steppingstones"
cd "$GAMEDIR" || exit 1

# find the Wayland compositor if there is one (ROCKNIX runs games inside Sway)
if [ -z "$XDG_RUNTIME_DIR" ]; then
  for d in /run/0-runtime-dir /var/run/0-runtime-dir /run/user/0 /tmp/runtime-root; do
    [ -d "$d" ] && export XDG_RUNTIME_DIR="$d" && break
  done
fi
if [ -z "$WAYLAND_DISPLAY" ] && [ -n "$XDG_RUNTIME_DIR" ]; then
  W="$(ls "$XDG_RUNTIME_DIR" 2>/dev/null | grep -E '^wayland-[0-9]+$' | head -1)"
  [ -n "$W" ] && export WAYLAND_DISPLAY="$W"
fi
[ -n "$WAYLAND_DISPLAY" ] && export SDL_VIDEODRIVER=wayland SDL_VIDEO_WAYLAND_WMCLASS=stones

# sound through PipeWire/PulseAudio when it's running, otherwise SDL picks ALSA
if command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1; then export SDL_AUDIODRIVER=pulse; fi

# the firmware's controller database, if it has one (so SDL knows the handheld's buttons)
for DB in /usr/config/SDL-GameControllerDB/gamecontrollerdb.txt /usr/share/SDL-GameControllerDB/gamecontrollerdb.txt \
          "$GAMEDIR/gamecontrollerdb.txt"; do
  [ -f "$DB" ] && export SDL_GAMECONTROLLERCONFIG_FILE="$DB" && break
done

./stones > "$GAMEDIR/log.txt" 2>&1
