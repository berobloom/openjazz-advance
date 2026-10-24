#!/bin/sh
# bench-run.sh ROM [OPTIONS...]: start level 1 and run right, jumping now and
# then; for example fps=2100:2600, sample=2100:2000c:FILE or shot=3000:FILE.png.
# Jazz first dies at about frame 2650, and the level loads again.
ROM=$1
shift
KEYS="keys=600:6:A keys=900:6:A keys=1200:6:A keys=2000:4000:r"
for f in $(seq 2100 150 6000); do KEYS="$KEYS keys=$f:20:A"; done
exec "$(dirname "$0")/build/ojgbarun" "$ROM" 4400 $KEYS "$@"
