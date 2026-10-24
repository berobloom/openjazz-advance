#!/bin/sh
# profsym.sh ELF SAMPLES: per-function histogram of the PCs in a prof= or
# sample= file. Names come from the symbol table, which also covers code copied
# to IWRAM; PCs in the BIOS are mostly waits for VBlank.
NM=${DEVKITARM:-/opt/devkitpro/devkitARM}/bin/arm-none-eabi-nm
"$NM" -n -C "$1" | awk '
	function hex(s,   v, i) {
		v = 0
		for (i = 1; i <= length(s); i++) v = v * 16 + index("0123456789abcdef", tolower(substr(s, i, 1))) - 1
		return v
	}

	# Function symbols, by address
	NR == FNR {
		if ($2 ~ /^[tTwW]$/) {
			name = $0
			sub(/^[^ ]+ [^ ]+ /, "", name)
			gsub(/\(anonymous namespace\)::/, "", name)
			sub(/\(.*/, "", name)
			n++
			addr[n] = hex($1)
			sym[n] = name
		}
		next
	}
	{
		pc = hex($1)
		if (pc < 16384) { count["[bios]"]++; total++; next }
		lo = 1; hi = n
		while (lo < hi) {
			mid = int((lo + hi + 1) / 2)
			if (addr[mid] <= pc) lo = mid; else hi = mid - 1
		}
		count[(addr[lo] <= pc)? sym[lo]: "??"]++
		total++
	}
	END {
		for (s in count) printf "%6.2f%% %s\n", 100 * count[s] / total, s
	}
' - "$2" | sort -rn | head -30
