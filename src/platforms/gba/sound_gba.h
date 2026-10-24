
/**
 *
 * @file sound_gba.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 */


#ifndef _SOUND_GBA_H
#define _SOUND_GBA_H

/// Call first thing in the VBlank interrupt
void gbaSoundVBlank ();

/// Call at the end of the VBlank interrupt: mixes the next frame of sound
void gbaSoundFrame ();

#endif
