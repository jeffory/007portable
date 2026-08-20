/**
 * @file skypool.h
 * GE draws sky clouds and water with hand-packed low-level RDP triangles
 * (G_TRI_SHADE_TXTR edge/attribute coefficient streams) that an F3D-HLE
 * renderer cannot execute. On PC, sky.c instead writes the triangle's three
 * vertices - already projected to screen space - into this pool and emits a
 * single G_TRIRAW_EXT command carrying the pool index; fast3d rebuilds the
 * triangle from the floats (port/fast3d/gfx_pc.cpp).
 *
 * Units match what the RDP coefficients would have encoded:
 *   x, y   screen coordinates in quarter-pixel units (viGetView* * 4 space)
 *   u, v   tile coordinates in s10.5 (fast3d applies tile shift and /32)
 *   w      per-vertex perspective weight (1/SkyRelated38.unk34); texture
 *          interpolation is perspective-corrected against it like the RDP's
 *          S/W, T/W planes
 *   rgba   shade color
 */
#ifndef PORT_SKYPOOL_H
#define PORT_SKYPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef G_TRIRAW_EXT
#define G_TRIRAW_EXT 0x46 /* keep in sync with port/fast3d/gbi_ext_modern.h */
#endif

struct PortSkyVtx {
    float x, y;
    float u, v;
    float w;
    unsigned char r, g, b, a;
};

#define PORT_SKY_TRI_POOL 8192

extern struct PortSkyVtx g_PortSkyTriPool[PORT_SKY_TRI_POOL][3];

#ifdef __cplusplus
}
#endif

#endif /* PORT_SKYPOOL_H */
