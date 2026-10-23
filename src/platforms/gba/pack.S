/*
 * pack.S
 *
 * Part of the OpenJazz project
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * Builds the baked game data pack (see io/bakepack.h) into the cartridge
 * ROM. CMake passes the pack's path as GBA_PACK_FILE.
 */

	.section .rodata

	.global gbaPackStart, gbaPackEnd

	.balign 4
gbaPackStart:
	.incbin GBA_PACK_FILE
gbaPackEnd:
