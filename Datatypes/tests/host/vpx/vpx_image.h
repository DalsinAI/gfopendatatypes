/*
 * vpx/vpx_image.h for building webm/webm_dither.h on x86 or ARM64 cores
 * without libvpx (tests/webmsizetest.c): only the fields and constants the
 * dither reads, under libvpx's names.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef VPX_VPX_IMAGE_H_
#define VPX_VPX_IMAGE_H_

#define VPX_PLANE_Y 0
#define VPX_PLANE_U 1
#define VPX_PLANE_V 2

typedef enum { VPX_CS_UNKNOWN = 0, VPX_CS_BT_601 = 1, VPX_CS_SRGB = 7 } vpx_color_space_t;

typedef struct vpx_image {
    vpx_color_space_t cs;
    unsigned int d_w, d_h;
    unsigned int x_chroma_shift, y_chroma_shift;
    unsigned char *planes[4];
    int stride[4];
} vpx_image_t;

#endif
