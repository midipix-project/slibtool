#ifndef SLIBTOOL_OUTPUT_H
#define SLIBTOOL_OUTPUT_H

#include <stdint.h>

#define SLBT_PRETTY(x)			((uint64_t)x << 32)

/* output actions */
#define SLBT_OUTPUT_ARCHIVE_MEMBERS	0x00000001
#define SLBT_OUTPUT_ARCHIVE_HEADERS	0x00000002
#define SLBT_OUTPUT_ARCHIVE_SYMBOLS	0x00000004
#define SLBT_OUTPUT_ARCHIVE_ARMAPS	0x00000008

/* pretty-printer flags */
#define SLBT_PRETTY_YAML		SLBT_PRETTY(0x00000001)
#define SLBT_PRETTY_POSIX		SLBT_PRETTY(0x00000002)
#define SLBT_PRETTY_HEXDATA		SLBT_PRETTY(0x00000004)
#define SLBT_PRETTY_VERBOSE		SLBT_PRETTY(0x00000008)

#endif
