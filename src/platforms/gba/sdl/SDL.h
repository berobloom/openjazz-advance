
/**
 *
 * @file SDL.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * A minimal stand-in for SDL 1.2 on the Game Boy Advance, providing what the
 * engine's SDL 1.2 code paths use and nothing more. Surfaces are always 8-bit.
 *
 * The engine only gives surfaces greyscale "logical" palettes in which each
 * colour's red value is the index it stands for (see Sprite::flashPalette()),
 * so a surface's logical palette is kept as a table mapping its pixel values
 * to red values, which blits apply. The physical palette of the video surface
 * is the GBA's background palette.
 *
 */


#ifndef OJ_GBA_SDL_H
#define OJ_GBA_SDL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;

typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

/* Initialisation */

#define SDL_INIT_TIMER    0x01
#define SDL_INIT_AUDIO    0x10
#define SDL_INIT_VIDEO    0x20
#define SDL_INIT_JOYSTICK 0x200

int         SDL_Init     (Uint32 flags);
void        SDL_Quit     (void);
const char* SDL_GetError (void);

typedef struct SDL_version {
	Uint8 major, minor, patch;
} SDL_version;

#define SDL_VERSION(x) do { (x)->major = 1; (x)->minor = 2; (x)->patch = 0; } while (0)
const SDL_version* SDL_Linked_Version (void);

/* Time */

Uint32 SDL_GetTicks (void);
void   SDL_Delay    (Uint32 ms);

/* Video */

typedef struct SDL_Color {
	Uint8 r, g, b, unused;
} SDL_Color;

typedef struct SDL_Rect {
	Sint16 x, y;
	Uint16 w, h;
} SDL_Rect;

typedef struct SDL_Palette {
	int        ncolors;
	SDL_Color* colors;
} SDL_Palette;

typedef struct SDL_PixelFormat {
	SDL_Palette* palette;
	Uint8        BitsPerPixel;
	Uint8        BytesPerPixel;
	Uint32       colorkey;
} SDL_PixelFormat;

typedef struct SDL_Surface {
	Uint32           flags;
	SDL_PixelFormat* format;
	int              w, h;
	Uint16           pitch;
	void*            pixels;
	SDL_Rect         clip_rect;

	/* GBA stand-in only */
	SDL_PixelFormat  formatData;
	Uint8*           remap; ///< Logical palette as pixel value -> index, or NULL for identity
} SDL_Surface;

#define SDL_SWSURFACE   0x00000000
#define SDL_HWSURFACE   0x00000001
#define SDL_HWPALETTE   0x20000000
#define SDL_DOUBLEBUF   0x40000000
#define SDL_FULLSCREEN  0x80000000
#define SDL_RESIZABLE   0x00000010
#define SDL_SRCCOLORKEY 0x00001000
#define SDL_PREALLOC    0x01000000

#define SDL_LOGPAL  0x01
#define SDL_PHYSPAL 0x02

#define SDL_MUSTLOCK(surface) 0

SDL_Surface* SDL_SetVideoMode         (int width, int height, int bpp, Uint32 flags);
SDL_Rect**   SDL_ListModes            (SDL_PixelFormat* format, Uint32 flags);
int          SDL_Flip                 (SDL_Surface* screen);
SDL_Surface* SDL_CreateRGBSurface     (Uint32 flags, int width, int height, int depth,
	Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
SDL_Surface* SDL_CreateRGBSurfaceFrom (void* pixels, int width, int height, int depth, int pitch,
	Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
void         SDL_FreeSurface          (SDL_Surface* surface);
int          SDL_LockSurface          (SDL_Surface* surface);
void         SDL_UnlockSurface        (SDL_Surface* surface);
int          SDL_SetPalette           (SDL_Surface* surface, int flags, const SDL_Color* colors, int first, int ncolors);
int          SDL_SetColors            (SDL_Surface* surface, const SDL_Color* colors, int first, int ncolors);
int          SDL_SetColorKey          (SDL_Surface* surface, Uint32 flag, Uint32 key);
SDL_bool     SDL_SetClipRect          (SDL_Surface* surface, const SDL_Rect* rect);
int          SDL_BlitSurface          (SDL_Surface* src, SDL_Rect* srcrect, SDL_Surface* dst, SDL_Rect* dstrect);
int          SDL_FillRect             (SDL_Surface* dst, SDL_Rect* rect, Uint32 color);
Uint32       SDL_MapRGB               (const SDL_PixelFormat* format, Uint8 r, Uint8 g, Uint8 b);
void         SDL_WM_SetCaption        (const char* title, const char* icon);

#define SDL_DISABLE 0
#define SDL_ENABLE  1
int          SDL_ShowCursor           (int toggle);

/* Events */

typedef enum {
	SDLK_UNKNOWN = 0,
	SDLK_BACKSPACE = 8,
	SDLK_TAB = 9,
	SDLK_RETURN = 13,
	SDLK_ESCAPE = 27,
	SDLK_SPACE = 32,
	SDLK_0 = 48, SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9,
	SDLK_a = 97, SDLK_b, SDLK_c, SDLK_d, SDLK_e, SDLK_f, SDLK_g, SDLK_h, SDLK_i, SDLK_j,
	SDLK_k, SDLK_l, SDLK_m, SDLK_n, SDLK_o, SDLK_p, SDLK_q, SDLK_r, SDLK_s, SDLK_t,
	SDLK_u, SDLK_v, SDLK_w, SDLK_x, SDLK_y, SDLK_z,
	SDLK_DELETE = 127,
	SDLK_UP = 273, SDLK_DOWN, SDLK_RIGHT, SDLK_LEFT,
	SDLK_F1 = 282, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6, SDLK_F7, SDLK_F8, SDLK_F9,
	SDLK_F10, SDLK_F11, SDLK_F12,
	SDLK_RSHIFT = 303, SDLK_LSHIFT, SDLK_RCTRL, SDLK_LCTRL, SDLK_RALT, SDLK_LALT,
	SDLK_LAST
} SDLKey;

typedef enum {
	KMOD_NONE = 0,
	KMOD_LALT = 0x100,
	KMOD_RALT = 0x200
} SDLMod;
#define KMOD_ALT (KMOD_LALT | KMOD_RALT)

typedef struct SDL_keysym {
	SDLKey sym;
	SDLMod mod;
	Uint16 unicode;
} SDL_keysym;

enum {
	SDL_NOEVENT = 0,
	SDL_KEYDOWN = 2,
	SDL_KEYUP,
	SDL_MOUSEMOTION,
	SDL_MOUSEBUTTONDOWN,
	SDL_MOUSEBUTTONUP,
	SDL_JOYAXISMOTION,
	SDL_JOYBALLMOTION,
	SDL_JOYHATMOTION,
	SDL_JOYBUTTONDOWN,
	SDL_JOYBUTTONUP,
	SDL_QUIT,
	SDL_VIDEORESIZE = 16,
	SDL_VIDEOEXPOSE
};

#define SDL_HAT_CENTERED 0x00
#define SDL_HAT_UP       0x01
#define SDL_HAT_RIGHT    0x02
#define SDL_HAT_DOWN     0x04
#define SDL_HAT_LEFT     0x08

#define SDL_BUTTON(x)   (1 << ((x) - 1))
#define SDL_BUTTON_LEFT 1

typedef struct { Uint8 type; Uint8 which; Uint8 state; SDL_keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint8 type; Uint8 which; Uint8 state; Uint16 x, y; Sint16 xrel, yrel; } SDL_MouseMotionEvent;
typedef struct { Uint8 type; Uint8 which; Uint8 button; Uint8 state; Uint16 x, y; } SDL_MouseButtonEvent;
typedef struct { Uint8 type; Uint8 which; Uint8 axis; Sint16 value; } SDL_JoyAxisEvent;
typedef struct { Uint8 type; Uint8 which; Uint8 hat; Uint8 value; } SDL_JoyHatEvent;
typedef struct { Uint8 type; Uint8 which; Uint8 button; Uint8 state; } SDL_JoyButtonEvent;
typedef struct { Uint8 type; int w, h; } SDL_ResizeEvent;

typedef union SDL_Event {
	Uint8                type;
	SDL_KeyboardEvent    key;
	SDL_MouseMotionEvent motion;
	SDL_MouseButtonEvent button;
	SDL_JoyAxisEvent     jaxis;
	SDL_JoyHatEvent      jhat;
	SDL_JoyButtonEvent   jbutton;
	SDL_ResizeEvent      resize;
} SDL_Event;

int   SDL_PollEvent  (SDL_Event* event);
char* SDL_GetKeyName (SDLKey key); ///< Name of the GBA button delivered as the key

/* Joysticks: the buttons are delivered as keys instead */

typedef struct _SDL_Joystick SDL_Joystick;

int           SDL_NumJoysticks  (void);
SDL_Joystick* SDL_JoystickOpen  (int index);
void          SDL_JoystickClose (SDL_Joystick* joystick);

#ifdef __cplusplus
}
#endif

#endif
