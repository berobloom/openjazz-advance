#!/bin/sh
# bench-still.sh ROM [OPTIONS...]: frame rate with Jazz standing at the start
# of level 1 (menus confirmed with A, then measured from frame 2200)
ROM=$1
shift
exec "$(dirname "$0")/build/ojgbarun" "$ROM" 3800 keys=600:6:A keys=900:6:A keys=1200:6:A fps=2200 "$@"
