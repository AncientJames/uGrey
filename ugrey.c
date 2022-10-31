#define MICROPY_PY_FRAMEBUF (1)

#include "py/dynruntime.h"

#include "framebuffer.h"

static inline int clamp(int v, int a, int b) {
    if (v <= a) return a;
    if (v >= b) return b;
    return v;
}

typedef struct {
    int width;
    int height;
    int grey_bits;
    int dither_bits;
} config_t;
config_t config;


////////////////////////////////////////////////////////////////////////////////
// set_backbuffer()
STATIC mp_obj_t set_backbuffer(mp_obj_t buf, mp_obj_t fmt) {

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_WRITE);
    /*if (bufinfo.typecode != BYTEARRAY_TYPECODE) {
        mp_raise_TypeError(NULL);
    }*/
    if (bufinfo.len < config.width) {
        mp_raise_ValueError(NULL);
    }

    framebuffer_set_buffer(mp_obj_get_int(fmt), -1, config.width, config.height, (uint8_t*)bufinfo.buf, bufinfo.len);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(set_backbuffer_obj, set_backbuffer);

////////////////////////////////////////////////////////////////////////////////
// blit_buffer()
STATIC mp_obj_t blit_buffer(mp_obj_t fb_in) {
    framebuffer_blit(fb_in);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(blit_buffer_obj, blit_buffer);


////////////////////////////////////////////////////////////////////////////////
// generator_config()
STATIC mp_obj_t generator_config(size_t n_args, const mp_obj_t *args) {
    if (n_args > 0 && mp_obj_get_int(args[0]) >= 0) {
        config.width = mp_obj_get_int(args[0]);
    }

    if (n_args > 1 && mp_obj_get_int(args[1]) >= 0) {
        config.height = mp_obj_get_int(args[1]);
    }

    if (n_args > 2 && mp_obj_get_int(args[2]) >= 0) {
        config.grey_bits = mp_obj_get_int(args[2]);
    }

    if (n_args > 3 && mp_obj_get_int(args[3]) >= 0) {
        config.dither_bits = mp_obj_get_int(args[3]);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(generator_config_obj, 0, 4, generator_config);

////////////////////////////////////////////////////////////////////////////////
// generate_page(pagebuffer, page, level, dither)
STATIC mp_obj_t generate_page(size_t n_args, const mp_obj_t *args) {

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_WRITE);
    if (bufinfo.len < config.width) {
        mp_raise_ValueError(NULL);
    }
    uint8_t *pagebuffer = (uint8_t*)bufinfo.buf;

    mp_int_t page = mp_obj_get_int(args[1]);

    mp_int_t level = mp_obj_get_int(args[2]);
    int level_bit = (0x80 >> level);

    mp_int_t dither = mp_obj_get_int(args[3]);
    dither *= 0x40 >> config.grey_bits;
    dither *= config.dither_bits;

    int y = config.height - 1 - page * 8;
    for (int x = 0; x < config.width; ++x) {
        dither = -dither;

        uint8_t byte = 0;
        for (int b = 0; b < 8; ++b) {
            dither = -dither;

            int c = (*framebuffer_get_pixel)(x, y - b);
            c = clamp(c + dither, 0, 255);

            byte = byte >> 1;
            if (c & level_bit) {
                byte |= 0x80;
            }
        }
        pagebuffer[x] = byte;
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR(generate_page_obj, 4, generate_page);


////////////////////////////////////////////////////////////////////////////////
// framebuf_get_buffer()
STATIC mp_obj_t framebuf_get_buffer(mp_obj_t fb_in) {

    mp_obj_t fb_obj = mp_obj_cast_to_native_base(fb_in, MP_OBJ_FROM_PTR(mp_type_framebuf));
    if (fb_obj == MP_OBJ_NULL) {
        mp_raise_TypeError(NULL);
    }
    mp_obj_framebuf_t *fb = MP_OBJ_TO_PTR(fb_obj);

    return fb->buf_obj;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(framebuf_get_buffer_obj, framebuf_get_buffer);

////////////////////////////////////////////////////////////////////////////////
// framebuf_get_format()
STATIC mp_obj_t framebuf_get_format(mp_obj_t fb_in) {

    mp_obj_t fb_obj = mp_obj_cast_to_native_base(fb_in, MP_OBJ_FROM_PTR(mp_type_framebuf));
    if (fb_obj == MP_OBJ_NULL) {
        mp_raise_TypeError(NULL);
    }
    mp_obj_framebuf_t *fb = MP_OBJ_TO_PTR(fb_obj);

    return mp_obj_new_int(fb->format);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(framebuf_get_format_obj, framebuf_get_format);

////////////////////////////////////////////////////////////////////////////////
// framebuf_get_dimensions()
STATIC mp_obj_t framebuf_get_dimensions(mp_obj_t fb_in) {

    mp_obj_t fb_obj = mp_obj_cast_to_native_base(fb_in, MP_OBJ_FROM_PTR(mp_type_framebuf));
    if (fb_obj == MP_OBJ_NULL) {
        mp_raise_TypeError(NULL);
    }
    mp_obj_framebuf_t *fb = MP_OBJ_TO_PTR(fb_obj);

    mp_obj_t tuple[3] = {
        mp_obj_new_int(fb->width),
        mp_obj_new_int(fb->height),
        mp_obj_new_int(fb->stride)
    };
    return mp_obj_new_tuple(3, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(framebuf_get_dimensions_obj, framebuf_get_dimensions);

////////////////////////////////////////////////////////////////////////////////
mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    MP_DYNRUNTIME_INIT_ENTRY

    framebuffer_init();
    framebuffer_import_framebuf();

    mp_store_global(MP_QSTR_set_backbuffer, MP_OBJ_FROM_PTR(&set_backbuffer_obj));
    mp_store_global(MP_QSTR_blit_buffer, MP_OBJ_FROM_PTR(&blit_buffer_obj));
    mp_store_global(MP_QSTR_generator_config, MP_OBJ_FROM_PTR(&generator_config_obj));
    mp_store_global(MP_QSTR_generate_page, MP_OBJ_FROM_PTR(&generate_page_obj));

    mp_store_global(MP_QSTR_framebuf_get_buffer, MP_OBJ_FROM_PTR(&framebuf_get_buffer_obj));
    mp_store_global(MP_QSTR_framebuf_get_format, MP_OBJ_FROM_PTR(&framebuf_get_format_obj));
    mp_store_global(MP_QSTR_framebuf_get_dimensions, MP_OBJ_FROM_PTR(&framebuf_get_dimensions_obj));

    config.width = 72;
    config.height = 40;
    config.grey_bits = 2;
    config.dither_bits = 1;

    MP_DYNRUNTIME_INIT_EXIT
}
