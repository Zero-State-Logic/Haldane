/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_port.h - the only portability knobs the core needs.
 *
 * The core is plain C99 with fixed-width integers. It never includes a
 * target header. Two things differ between the host build and avr-gcc:
 *
 *   HD_ROM  Qualifier for constant lookup tables. On the AVR build the
 *           Makefile passes -DHD_ROM=__flash (GNU C named address space),
 *           so tables stay in flash instead of being copied into the
 *           2 KB of SRAM. On the host it expands to nothing.
 *
 *   int     Is 16 bits on AVR. Every constant wider than 16 bits in the
 *           core is written with an explicit INT32_C/UINT32_C or cast.
 */
#ifndef HD_PORT_H
#define HD_PORT_H

#include <stdint.h>

#ifndef HD_ROM
#define HD_ROM
#endif

/* Status codes returned by init/configuration functions. */
#define HD_OK          0
#define HD_ERR_NULL   -1
#define HD_ERR_RANGE  -2

/* C99-compatible compile-time assertion (no _Static_assert in C99). */
#define HD_STATIC_ASSERT(name, cond) typedef char hd_static_assert_##name[(cond) ? 1 : -1]

#endif /* HD_PORT_H */
