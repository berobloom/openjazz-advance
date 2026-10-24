#!/bin/sh
# detcompare.sh OLD_BIN NEW_BIN DATA_DIR [FRAMES]: run two SDL2 desktop builds
# through the menu demos with detshim and list the frames whose output differs.
# Both builds must show the same version string, which the menu draws.
HERE=$(dirname "$0")
OUT=${TMPDIR:-/tmp}/detcompare.$$
mkdir -p "$OUT/home"
FRAMES=${4:-12000}
run () {
	HOME="$OUT/home" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy DET_FRAMES=$FRAMES DET_OUT="$OUT/$2.txt" \
		LD_PRELOAD="$HERE/build/detshim.so" "$1" -q --no-logfile "$3" > /dev/null 2>&1
}
run "$1" old "$3" &
run "$2" new "$3" &
wait
paste "$OUT/old.txt" "$OUT/new.txt" | awk '$2 != $4 { n++; print "differs: frame " $1 } END { print n + 0 " of " NR " frames differ" }'
rm -rf "$OUT"
