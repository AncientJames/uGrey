#ifndef _FRAMEBUFFER_H_
#define _FRAMEBUFFER_H_


typedef struct _mp_obj_framebuf_t {
    mp_obj_base_t base;
    mp_obj_t buf_obj;
    void *buf;
    uint16_t width, height, stride;
    uint8_t format;
} mp_obj_framebuf_t;

extern mp_obj_type_t *mp_type_framebuf;


typedef enum {
    FORMAT_MONO_VLSB  = 0,
    FORMAT_RGB565     = 1,
    FORMAT_GS4_HMSB   = 2,
    FORMAT_MONO_HLSB  = 3,
    FORMAT_MONO_HMSB  = 4,
    FORMAT_GS2_HMSB   = 5,
    FORMAT_GS8        = 6
} framebuffer_format_t;

typedef struct {
    uint8_t *buffer;
    int buffer_len;
    int stride;
    int width;
    int height;
} framebuffer_info_t;

extern framebuffer_info_t framebuffer_info;

extern int framebuffer_dither;

extern int framebuffer_bpp(framebuffer_format_t format);
extern int framebuffer_norm_to_native(int c, framebuffer_format_t format);
extern int framebuffer_native_to_norm(int c, framebuffer_format_t format);

extern int (*framebuffer_get_pixel)(int x, int y);
extern void (*framebuffer_set_pixel)(int x, int y, int c);
extern void (*framebuffer_blend_pixel)(int x, int y, int c, int a);

extern void framebuffer_blit(mp_obj_t fb_src);

extern void framebuffer_set_framebuf(mp_obj_t obj);
extern void framebuffer_set_buffer(framebuffer_format_t format, int stride, int width, int height, uint8_t *buf, size_t len);
extern void framebuffer_unset();


extern void framebuffer_import_framebuf();
extern void framebuffer_init();



#endif
