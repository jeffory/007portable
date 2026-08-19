/**
 * @file math_sincos.c
 * Replaces src/game/math_sincos.s (hand-written Taylor-series sinf/cosf)
 * with libm. Values differ from the N64 in the last ulps; revisit if
 * demo-playback sync ever matters.
 */
#include <math.h>

/* libm provides sinf/cosf; nothing to define. This file exists so the
 * build system documents where the assembly went. If a bit-exact port of
 * the N64 approximations is ever needed, implement them here and adjust
 * the link order. */
