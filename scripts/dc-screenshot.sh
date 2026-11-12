#!/usr/bin/env bash
# Headless screenshot of the current project's game: runs it on a virtual X display with
# software OpenGL, waits, grabs the screen. Handy for checking rendering without a monitor
# (or for letting an AI assistant look at the result). Run from a project directory.
#
#   dc-screenshot.sh pc [out.png] [seconds]   runs build/linux-<type>/game          (default 5s)
#   dc-screenshot.sh dc [out.png] [seconds]   boots build/dreamcast-<type>/game.cdi in Flycast (default 15s)
#
# The screenshot is taken [seconds] after launch. On the Dreamcast that includes booting
# (~8 s until main() runs). From a project: make shot-pc / make shot-dc [WAIT=<seconds>].
#
# TYPE=debug picks the debug build. The program's output goes to a .log file next to the .png
# (for dc, that includes the Dreamcast's serial console: printf / Simulant log lines).
set -euo pipefail

TARGET="${1:?usage: $0 pc|dc [out.png] [seconds]}"
PROJECT="$(pwd)"
TYPE="${TYPE:-release}"
OUT="$(realpath -m "${2:-$PROJECT/build/screenshot-$TARGET.png}")"
LOG="${OUT%.png}.log"     # next to the screenshot, so every run keeps its own log

case "$TARGET" in
  pc) WAIT="${3:-5}";  CMD="cd '$PROJECT/build/linux-$TYPE' && ./game" ;;
  dc) WAIT="${3:-15}"; CMD="flycast -config config:Debug.SerialConsoleEnabled=yes '$PROJECT/build/dreamcast-$TYPE/game.cdi'" ;;
  *) echo "unknown target: $TARGET (use pc or dc)" >&2; exit 1 ;;
esac

mkdir -p "$PROJECT/build"
env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a -s '-screen 0 640x480x24' bash -c "
    ( $CMD ) > '$LOG' 2>&1 &
    sleep $WAIT
    xwd -root -silent | magick xwd:- '$OUT'
    kill %1 2>/dev/null; sleep 0.5; pkill -P \$\$ 2>/dev/null || true"
echo "$OUT"
