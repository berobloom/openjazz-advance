#ifndef FLAGS_H
#define FLAGS_H
/* Stands in for mGBA's generated flags.h, so the runner can use the headers of
   an mGBA 0.10.2 source tree with a distribution's libmgba 0.10 (which ships
   no headers). Matches the Debian build closely enough for the mCore API. */
#define M_CORE_GBA
#define M_CORE_GB
#define USE_DEBUGGERS
#define ENABLE_SCRIPTING
#endif
