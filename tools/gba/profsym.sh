#!/bin/sh
# profsym.sh ELF SAMPLES: per-function histogram of the PCs a prof= run sampled
ADDR2LINE=${DEVKITARM:-/opt/devkitpro/devkitARM}/bin/arm-none-eabi-addr2line
awk '{print $1}' "$2" | "$ADDR2LINE" -f -C -e "$1" | awk 'NR%2==1' | sed -e 's/(anonymous namespace):://g' -e 's/(.*//' | sort | uniq -c | sort -rn | head -25
