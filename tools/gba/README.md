# GBA port test tools

Tools used while porting OpenJazz to the Game Boy Advance. None of them are
part of the game build. `make` builds them into `build/` here.

Build requirements: gcc, zlib, the SDL2 headers, git (to fetch the
mGBA 0.10.2 headers) and the system's `libmgba.so.0.10` (Debian/Ubuntu package
`libmgba0.10t64`). Set `LIBMGBA=/path/to/libmgba.so.0.10` if it is somewhere
else.

Never share a ROM or pack made from the game data.

## ojgbarun

Headless GBA runner. It runs a ROM for a number of frames, presses buttons on a
schedule, saves screenshots and prints the ROM's debug log. See the comment at
the top of `runner/ojgbarun.c` for all options.

    build/ojgbarun ../../build-gba/OpenJazz.gba 1200 keys=600:6:A shot=1190:menu.png

At the end it prints the CPU registers and the ROM addresses found on the stack,
which helps with hangs and crashes. Look the addresses up with
`arm-none-eabi-addr2line -e OpenJazz.elf`.

- `fps=FROM[:TO]` counts page flips, which gives the game's own frame rate.
  In levels the port toggles the page bit with each frame shown, so this
  works there too.
- `prof=FROM:FILE` samples the PC once per frame. `profsym.sh ELF FILE` turns
  the samples into a per-function histogram.
- `sample=FROM:N:FILE` single-steps from frame FROM and writes the PC and LR
  every N instructions, or every N cycles with `Nc` (for example `2000c`).
  Counting cycles shows where the time goes; counting instructions overstates
  quick code. `profsym.sh` reads these samples too.
- `logframes` starts each line of the ROM's log with the frame number.
- `stackfree=ADDR` reports how much of the IWRAM stack was never touched. Pass
  the ROM's `__iheap_start`, from `arm-none-eabi-nm OpenJazz.elf`. The port
  fills the stack with a pattern at startup (see `src/platforms/gba.cpp`).
  Keep about 6 KB free.

## Benchmarks

    ./bench-still.sh ROM    # Jazz standing at the start of level 1
    ./bench-run.sh ROM fps=2200 shot=3000:run.png

Use the standing benchmark to compare builds. While running, the scene depends
on how fast the game runs, so two builds don't draw the same frames. Jazz also
loses lives on the way: the first death and level reload come at about frame
2650, so measure the running frame rate over `fps=2100:2600`.

## Blitter tests

    make test

Random blits through `src/platforms/gba/blit_gba.iwram.cpp`, built for the host
and compared with plain reference loops. Pass a number of cases to run more:
`build/scaletest 200000`.

## detshim and detcompare.sh

Use these to check that an engine change leaves the desktop game's output alone.
`detshim.so` is an `LD_PRELOAD` shim for the SDL2 build. It fixes time so the
menu demos play the same way every run, and it hashes every presented frame.

    ./detcompare.sh old/OpenJazz new/OpenJazz ../../gamedata/Jazz

This runs both builds for 12000 frames and lists the frames that differ. Both
builds must show the same version string, because the menu draws it. For
example, define `OJ_VERSION_GIT` in one build's `src/version.cpp`. The shim runs the
game without audio, since the audio thread runs in real time. Builds from
before the palette alpha fix (see `Video::opaqueColors`) still show the odd
frame that differs from run to run, on screen transitions such as frames 3400
and 11178.

To reach other parts of the game, run the shim directly. `DET_KEYS` presses
keys (SDL keycodes, in decimal) at given frames, for a given number of frames.
`DET_DUMP` saves frames as `DET_OUT.N.ppm`. For example, this starts level 2
and holds Up for 600 frames:

    HOME=/tmp/dethome SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    DET_FRAMES=1000 DET_OUT=/tmp/run.txt DET_KEYS="100:13,400:1073741906:600" \
    DET_DUMP=",500,900," LD_PRELOAD=build/detshim.so OpenJazz -q --no-logfile -w 0 -l 1 DATA_DIR
