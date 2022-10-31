#include "py/dynruntime.h"

#include "framebuffer.h"

mp_obj_type_t *mp_type_framebuf;
mp_obj_t framebuf_pixel_obj;


mp_obj_framebuf_t *framebuffer_framebuf;

framebuffer_info_t framebuffer_info;

int framebuffer_dither;

int (*framebuffer_get_pixel)(int, int);
void (*framebuffer_set_pixel)(int, int, int);
void (*framebuffer_blend_pixel)(int, int, int, int);


#if !defined(__linux__)
void *memset(void *s, int c, size_t n) {
    return mp_fun_table.memset_(s, c, n);
}
void *memmove(void *d, void* s, size_t n) {
    return mp_fun_table.memmove_(d, s, n);
}
#endif

#define BYTE_DIV(n, d) (((uint32_t)(n) * (uint32_t)(((1 << 24) + (d - 1)) / d)) >> 24)

static inline int clamp(int v, int a, int b) {
    if (v <= a) return a;
    if (v >= b) return b;
    return v;
}


int framebuffer_bpp(framebuffer_format_t format) {
    static const int bpp[8] = {
        1, // FORMAT_MONO_VLSB
       16, // FORMAT_RGB565
        4, // FORMAT_GS4_HMSB
        1, // FORMAT_MONO_HLSB
        1, // FORMAT_MONO_HMSB
        2, // FORMAT_GS2_HMSB
        8  // FORMAT_GS8
    };

    return bpp[format & 7];
}

int framebuffer_norm_to_native(int c, framebuffer_format_t format) {
    switch (format) {
        case FORMAT_MONO_VLSB:
        case FORMAT_MONO_HLSB:
        case FORMAT_MONO_HMSB:
            return c >= 128;

        case FORMAT_GS2_HMSB:
            return BYTE_DIV(c, 85);

        case FORMAT_GS4_HMSB:
            return BYTE_DIV(c, 17);

        case FORMAT_RGB565:
            return ((c >> 3) & 31) << 11 | ((c >> 2) & 63) << 5 | ((c >> 3) & 31);

        default:
            return c;
    }
}

int framebuffer_native_to_norm(int c, framebuffer_format_t format) {
    switch (format) {
        case FORMAT_MONO_VLSB:
        case FORMAT_MONO_HLSB:
        case FORMAT_MONO_HMSB:
            return c ? 255 : 0;

        case FORMAT_GS2_HMSB:
            return c * 85;

        case FORMAT_GS4_HMSB:
            return c * 17;

        case FORMAT_RGB565:
            return ((c >> 11) + (c & 31) + ((c >> 5) & 63)) * 2;

        default:
            return c;
    }
}

static void set_pixel_framebuf(int x, int y, int c) {
    mp_obj_framebuf_t *fb = MP_OBJ_TO_PTR(framebuffer_framebuf);

    if (framebuffer_dither) {
        c = clamp(c + framebuffer_dither * (((x ^ y) & 1) * 84 - 42), 0, 255);
    }
    c = framebuffer_norm_to_native(c, fb->format);

    mp_obj_t args[4] = {
        framebuffer_framebuf,
        MP_OBJ_NEW_SMALL_INT(x),
        MP_OBJ_NEW_SMALL_INT(y),
        MP_OBJ_NEW_SMALL_INT(c)
    };
    mp_call_function_n_kw(framebuf_pixel_obj, 4, 0, args);
}

static int get_pixel_framebuf(int x, int y) {
    mp_obj_framebuf_t *fb = MP_OBJ_TO_PTR(framebuffer_framebuf);

    mp_obj_t args[3] = {
        framebuffer_framebuf,
        MP_OBJ_NEW_SMALL_INT(x),
        MP_OBJ_NEW_SMALL_INT(y),
    };
    int c = mp_obj_get_int(mp_call_function_n_kw(framebuf_pixel_obj, 3, 0, args));

    return framebuffer_native_to_norm(c, fb->format);
}

static void blend_pixel_framebuf(int x, int y, int c, int a) {
    if (a > 0) {
        if (a < 255) {
            int dst = get_pixel_framebuf(x, y);
            c = BYTE_DIV(((c * a) + (dst * (255 - a))), 255);
        }
        set_pixel_framebuf(x, y, c);
    }
}

static int get_pixel_none(int x, int y) {
    return 0;
}

static void set_pixel_none(int x, int y, int c) {
}

static void blend_pixel_none(int x, int y, int c, int a) {
}

static int get_pixel_mono_vlsb(int x, int y) {
    uint8_t *byte = &framebuffer_info.buffer[x + (y / 8) * framebuffer_info.stride];
    return ((*byte) & (0x01 << (y & 7))) ? 255 : 0;
}
static void set_pixel_mono_vlsb(int x, int y, int c) {
    uint8_t *byte = &framebuffer_info.buffer[x + (y / 8) * framebuffer_info.stride];
    if (framebuffer_dither) {
        c += framebuffer_dither * (((x ^ y) & 1) * 84 - 42);
    }    
    if (c > 127) {
        *byte |= (0x01 << (y & 7));
    } else {
        *byte &= ~(0x01 << (y & 7));
    }
}
static void blend_pixel_mono_vlsb(int x, int y, int c, int a) {
    if (a > 127) {
        set_pixel_mono_vlsb(x, y, c);
    }
}

static int get_pixel_mono_hlsb(int x, int y) {
    uint8_t *byte = &framebuffer_info.buffer[(x + y * framebuffer_info.stride) / 8];
    return ((*byte) & (0x80 >> (x & 7))) ? 255 : 0;
}
static void set_pixel_mono_hlsb(int x, int y, int c) {
    uint8_t *byte = &framebuffer_info.buffer[(x + y * framebuffer_info.stride) / 8];
    if (framebuffer_dither) {
        c += framebuffer_dither * (((x ^ y) & 1) * 84 - 42);
    }    
    if (c > 127) {
        *byte |= (0x80 >> (x & 7));
    } else {
        *byte &= ~(0x80 >> (x & 7));
    }
}
static void blend_pixel_mono_hlsb(int x, int y, int c, int a) {
    if (a > 127) {
        set_pixel_mono_hlsb(x, y, c);
    }
}

static int get_pixel_mono_hmsb(int x, int y) {
    uint8_t *byte = &framebuffer_info.buffer[(x + y * framebuffer_info.stride) / 8];
    return ((*byte) & (0x01 << (x & 7))) ? 255 : 0;
}
static void set_pixel_mono_hmsb(int x, int y, int c) {
    uint8_t *byte = &framebuffer_info.buffer[(x + y * framebuffer_info.stride) / 8];
    if (framebuffer_dither) {
        c += framebuffer_dither * (((x ^ y) & 1) * 84 - 42);
    }    
    if (c > 127) {
        *byte |= (0x01 << (x & 7));
    } else {
        *byte &= ~(0x01 << (x & 7));
    }
}
static void blend_pixel_mono_hmsb(int x, int y, int c, int a) {
    if (a > 127) {
        set_pixel_mono_hmsb(x, y, c);
    }
}

static int get_pixel_gs2_hmsb(int x, int y) {
    uint8_t *byte = &framebuffer_info.buffer[(x + y * framebuffer_info.stride) / 4];
    int offset = (x & 3) * 2;
    return (((*byte) >> offset) & 0x03) * 85;
}
static void set_pixel_gs2_hmsb(int x, int y, int c) {
    uint8_t *byte = &framebuffer_info.buffer[(x + y * framebuffer_info.stride) / 4];
    int offset = (x & 3) * 2;
    *byte &= ~(0x03 << offset);
    *byte |= (BYTE_DIV(c, 85)) << offset;
}
static void blend_pixel_gs2_hmsb(int x, int y, int c, int a) {
    if (a > 0) {
        uint8_t* byte = &framebuffer_info.buffer[(x + y * framebuffer_info.stride) / 4];
        int offset = (x & 3) * 2;
        if (a < 255) {
            int dst = (((*byte) >> offset) & 0x03) * 85;
            c = BYTE_DIV(((c * a) + (dst * (255 - a))), 255);
        }
        *byte &= ~(0x03 << offset);
        *byte |= (BYTE_DIV(c, 85)) << offset;
    }
}

static int get_pixel_gs4_hlsb(int x, int y) {
    uint8_t *byte = &framebuffer_info.buffer[(x + y * framebuffer_info.stride) / 2];
    int offset = ((1 - (x & 1)) * 4);
    return (((*byte) >> offset) & 0x0f) * 17;
}
static void set_pixel_gs4_hlsb(int x, int y, int c) {
    uint8_t *byte = &framebuffer_info.buffer[(x + y * framebuffer_info.stride) / 2];
    int offset = ((1 - (x & 1)) * 4);
    *byte &= ~(0x0f << offset);
    *byte |= (BYTE_DIV(c, 17)) << offset;
}
static void blend_pixel_gs4_hlsb(int x, int y, int c, int a) {
    if (a > 0) {
        uint8_t* byte = &framebuffer_info.buffer[(x + y * framebuffer_info.stride) / 2];
        int offset = ((1 - (x & 1)) * 4);
        if (a < 255) {
            int dst = (((*byte) >> offset) & 0x0f) * 17;
            c = BYTE_DIV(((c * a) + (dst * (255 - a))), 255);
        }
        *byte &= ~(0x0f << offset);
        *byte |= (BYTE_DIV(c, 17)) << offset;
    }
}

static int get_pixel_gs8(int x, int y) {
    return framebuffer_info.buffer[x + y * framebuffer_info.stride];
}
static void set_pixel_gs8(int x, int y, int c) {
    framebuffer_info.buffer[x + y * framebuffer_info.stride] = c;
}
static void blend_pixel_gs8(int x, int y, int c, int a) {
    if (a > 0) {
        uint8_t* byte = &framebuffer_info.buffer[x + y * framebuffer_info.stride];
        if (a < 255) {
            c = BYTE_DIV(((c * a) + ((*byte) * (255 - a))), 255);
        }
        *byte = c;
    }
}


static int get_pixel_rgb565(int x, int y) {
    uint word = ((uint16_t*)framebuffer_info.buffer)[(x + y * framebuffer_info.stride) * 2];
    return (((word >> 11) & 0x1f) + ((word >> 5) & 0x3f) + (word & 0x1f)) * 2;
}

void framebuffer_blit(mp_obj_t fb_src) {

    mp_obj_t fb_obj = mp_obj_cast_to_native_base(fb_src, MP_OBJ_FROM_PTR(mp_type_framebuf));
    if (fb_obj == MP_OBJ_NULL) {
        mp_raise_TypeError(NULL);
    }
    mp_obj_framebuf_t *fb = MP_OBJ_TO_PTR(fb_obj);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(fb->buf_obj, &bufinfo, MP_BUFFER_READ);

    if (framebuffer_info.buffer && framebuffer_info.buffer_len == bufinfo.len) {
        memmove(framebuffer_info.buffer, bufinfo.buf, bufinfo.len);
    }
}

void framebuffer_set_framebuf(mp_obj_t obj) {

    framebuffer_framebuf = mp_obj_cast_to_native_base(obj, MP_OBJ_FROM_PTR(mp_type_framebuf));

    if (framebuffer_framebuf == MP_OBJ_NULL) {
        mp_raise_TypeError(NULL);
    }

    mp_obj_framebuf_t *fb = MP_OBJ_TO_PTR(framebuffer_framebuf);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(fb->buf_obj, &bufinfo, MP_BUFFER_RW);

    framebuffer_set_buffer(fb->format, fb->stride, fb->width, fb->height, bufinfo.buf, bufinfo.len);

    if (framebuffer_get_pixel == get_pixel_none) {
        framebuffer_get_pixel = get_pixel_framebuf;
        framebuffer_set_pixel = set_pixel_framebuf;
        framebuffer_blend_pixel = blend_pixel_framebuf;
    }
}

void framebuffer_set_buffer(framebuffer_format_t format, int stride, int width, int height, uint8_t *buf, size_t len) {

    framebuffer_info.buffer = buf;
    framebuffer_info.buffer_len = len;
    framebuffer_info.stride = stride > 0 ? stride : width;
    framebuffer_info.width = width;
    framebuffer_info.height = height;

    switch (format) {
        case FORMAT_MONO_VLSB:
            framebuffer_get_pixel = &get_pixel_mono_vlsb;
            framebuffer_set_pixel = &set_pixel_mono_vlsb;
            framebuffer_blend_pixel = &blend_pixel_mono_vlsb;
            break;
            
        case FORMAT_MONO_HLSB:
            framebuffer_get_pixel = &get_pixel_mono_hlsb;
            framebuffer_set_pixel = &set_pixel_mono_hlsb;
            framebuffer_blend_pixel = &blend_pixel_mono_hlsb;
            break;

        case FORMAT_MONO_HMSB:
            framebuffer_get_pixel = &get_pixel_mono_hmsb;
            framebuffer_set_pixel = &set_pixel_mono_hmsb;
            framebuffer_blend_pixel = &blend_pixel_mono_hmsb;
            break;

        case FORMAT_GS2_HMSB:
            framebuffer_get_pixel = &get_pixel_gs2_hmsb;
            framebuffer_set_pixel = &set_pixel_gs2_hmsb;
            framebuffer_blend_pixel = &blend_pixel_gs2_hmsb;
            break;

        case FORMAT_GS4_HMSB:
            framebuffer_get_pixel = &get_pixel_gs4_hlsb; // match framebuf behaviour
            framebuffer_set_pixel = &set_pixel_gs4_hlsb;
            framebuffer_blend_pixel = &blend_pixel_gs4_hlsb;
            break;

        case FORMAT_RGB565:
            framebuffer_get_pixel = &get_pixel_rgb565;
            framebuffer_set_pixel = &set_pixel_framebuf;
            framebuffer_blend_pixel = &blend_pixel_framebuf;
            break;

        case FORMAT_GS8:
            framebuffer_get_pixel = &get_pixel_gs8;
            framebuffer_set_pixel = &set_pixel_gs8;
            framebuffer_blend_pixel = &blend_pixel_gs8;
            break;

        default:
            framebuffer_info.stride = 0;
            framebuffer_get_pixel = &get_pixel_none;
            framebuffer_set_pixel = &set_pixel_none;
            framebuffer_blend_pixel = &blend_pixel_none;
            break;
    }
}

void framebuffer_unset() {
    framebuffer_framebuf = NULL;
    memset(&framebuffer_info, 0, sizeof(framebuffer_info));
    framebuffer_get_pixel = &get_pixel_none;
    framebuffer_set_pixel = &set_pixel_none;
    framebuffer_blend_pixel = &blend_pixel_none;
}

void framebuffer_init() {
    framebuffer_unset();
    framebuffer_dither = 0;
}

void framebuffer_import_framebuf() {
    mp_obj_t modframebuf = mp_import_name(MP_QSTR_framebuf, mp_const_none, 0);
    mp_type_framebuf = MP_OBJ_TO_PTR(mp_load_attr(modframebuf, MP_QSTR_FrameBuffer));

    mp_obj_t dest[2];
    mp_load_method(MP_OBJ_FROM_PTR(mp_type_framebuf), MP_QSTR_pixel, dest);

    framebuf_pixel_obj = dest[0];
    mp_store_global(MP_QSTR_pixel, framebuf_pixel_obj);
}

