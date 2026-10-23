
/**
 *
 * @file platforms.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * Copyright (c) 2015-2026 Carsten Teibes
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 */


#ifndef _PLATFORMS_H
#define _PLATFORMS_H

#include "OpenJazz.h"
#include "3ds.h"
#include "apple.h"
#include "android.h"
#include "dingoo.h"
#include "emscripten.h"
#include "gameshell.h"
#include "gba.h"
#include "gp2x_wiz_canoo.h"
#include "haiku.h"
#include "psp.h"
#include "psvita.h"
#include "riscos.h"
#include "switch.h"
#include "wii.h"
#include "windows.h"
#include "xdg.h"

#include "platform_interface.h"

/// Marks a function the platform should place in its fastest memory
#ifndef OJ_FAST_CODE
	#define OJ_FAST_CODE
#endif

class DefaultPlatform final : public IPlatform {
};

EXTERN IPlatform* platform;

#endif
