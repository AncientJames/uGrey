import framebuf
import machine
import time
import _thread
import ugrey
import micropython


def format_bpp(format):
    if (format == framebuf.MONO_VLSB
     or format == framebuf.MONO_HLSB
     or format == framebuf.MONO_HMSB):
        return 1
    if format == framebuf.GS2_HMSB:
        return 2
    if format == framebuf.GS4_HMSB:
        return 4
    if format == framebuf.GS8:
        return 8
    if format == framebuf.RGB565:
        return 16
    return 8

def viewport(framebuffer, x, y, w, h):
    (fbw, fbh, fbs) = ugrey.framebuf_get_dimensions(framebuffer)
    format = ugrey.framebuf_get_format(framebuffer)
    bpp = ugrey.format_bpp(format)

    x0 = max(0, ((x * bpp) // 8) * (8 // bpp))
    y0 = max(0, y)
    x1 = min(fbw, ((((x + w) * bpp) + 7) // 8) * (8 // bpp))
    y1 = min(fbh, y + h)

    x, y, w, h = x0, y0, x1 - x0, y1 - y0
    if w <= 0 or h <= 0:
        print(w)
        return None

    ba = ugrey.framebuf_get_buffer(framebuffer)
    mv = memoryview(ba)
    offset = ((x + y * fbs) * bpp) // 8

    return framebuf.FrameBuffer(mv[offset:], w, h, format, fbs)

class Device:
    command_initialise = bytearray([
        0xAE,           #display off
        0xD5, 0xF0,     #set display clock divide
        0xA8, 39,       #set multiplex ratio 39
        0xD3, 0x00,     #set display offset
        0x40,           #set display start line 0
        0x8D, 0x14,     #set charge pump enabled (0x14:7.5v 0x15:6.0v 0x94:8.5v 0x95:9.0v)
        0x20, 0x00,     #set addressing mode horizontal
        0xA1,           #set segment remap (0=seg0)
        0xC0,           #set com scan direction
        0xDA, 0x12,     #set alternate com pin configuration
        0xAD, 0x30,     #internal iref enabled (0x30:240uA 0x10:150uA)
        0x81, 0x01,     #set display_contrast
        0xD9, 0x11,     #set pre-charge period
        0xDB, 0x20,     #set vcomh deselect
        0xA4,           #unset entire display on
        0xA6,           #unset inverse display
        0x21, 28, 99,   #set column address / start 28 / end 99
        0x22, 0, 4,     #set page address / start 0 / end 4
        0xAF            # set display on
    ])
        
    def preset(self, bpp):
        if bpp > 4:        
            self.format = framebuf.GS8
            self.grey_bits = 3
            self.dither_bits = 1

        elif bpp == 4:
            self.format = framebuf.GS4_HMSB
            self.grey_bits = 3
            self.dither_bits = 1

        elif bpp == 3:
            self.format = framebuf.GS4_HMSB
            self.grey_bits = 2
            self.dither_bits = 1

        elif bpp == 2:
            self.format = framebuf.GS2_HMSB
            self.grey_bits = 2
            self.dither_bits = 0

        else:
            self.format = framebuf.MONO_HMSB
            self.grey_bits = 1
            self.dither_bits = 0

    def hw_defaults(self):
        if self.spi == None:
            self.spi = machine.SPI(0, baudrate=62500000, sck=machine.Pin(18), mosi=machine.Pin(19))
        if self.cs == None:
            self.cs = machine.Pin(16)
        if self.res == None:
            self.res = machine.Pin(20)
        if self.dc == None:
            self.dc = machine.Pin(17)

    def __init__(self, bpp=4, spi=None, res=None, cs=None, dc=None):
        self.sync_lock = _thread.allocate_lock()

        self.spi = spi
        self.cs = cs
        self.res = res
        self.dc = dc
        self.hw_defaults()

        self.width = 72
        self.height = 40

        self.temporal_dither = 1
        self.preset(bpp)

        self.contrast = 255
        
        self.frame_period = 5500
        self.park_lines = 2
        
        self.framebuffer = None
        self._backbuffer = None

        self._running = False
        self._stopped = True

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        self.stop()
        
    def start(self):
        if self.framebuffer == None:            
            buflen = (self.width * self.height * format_bpp(self.format)) // 8
            self.framebuffer = framebuf.FrameBuffer(bytearray(buflen), self.width, self.height, self.format)

        self.format = ugrey.framebuf_get_format(self.framebuffer)

        fb_buf = ugrey.framebuf_get_buffer(self.framebuffer)
        if self._backbuffer == None or len(self._backbuffer) != len(fb_buf):
            self._backbuffer = bytearray(len(fb_buf))

        if self.spi == None:
            self.spi = machine.SPI(0, baudrate=62500000, sck=machine.Pin(6), mosi=machine.Pin(7))

        self.cs.init(machine.Pin.OUT)
        self.res.init(machine.Pin.OUT)
        self.dc.init(machine.Pin.OUT)
        
        self.res(0)
        time.sleep_ms(1)
        self.res(1)

        self.cs(1)
        
        self.dc(0)
        self.cs(0)
        self.spi.write(self.command_initialise)
        self.cs(1)
        
        self._running = True
        self._stopped = False
        _thread.start_new_thread(self._worker, ())
        
        time.sleep_us(self.frame_period * 2)

    def stop(self):
        self._running = False
        while self._stopped == False:
            time.sleep(0.01)

    def show(self, vsync=True):
        if vsync:
            self.sync_lock.acquire()

        ugrey.blit_buffer(self.framebuffer)

        if vsync:
            self.sync_lock.release()


    def _worker(self):
        command_park = bytearray([
            0xA8, self.park_lines-1,#set minimum multiplex
            0xD3, 4,                #set display offset off the top
        ])

        command_run = bytearray([
            0x81, 1,                #set contrast
            0xD3, 0,                #reset display offset
            0xA8, int(self.height) + 12 - 1,       #multiplex + overscan
        ])


        ugrey.generator_config(
            self.width,
            self.height,
            self.grey_bits,
            self.dither_bits
        )
        ugrey.set_backbuffer(
            self._backbuffer,
            self.format
        )
        
        page_buffer = bytearray(self.width)
        page_count = int(self.height) // 8

        cs = self.cs
        dc = self.dc
        spi = self.spi        

        frame_time = time.ticks_us()

        level:int = 0
        dither:int = 1

        while self._running:
            cs(0)

            dc(0)
            spi.write(command_park)

            if level == 0:
                self.sync_lock.acquire()
            
            dc(1)
            for p in range(page_count):
                ugrey.generate_page(page_buffer, p, level, dither)
                spi.write(page_buffer)

            dc(0)
            command_run[1] = (int(self.contrast) >> (level << 1))
            spi.write(command_run)

            cs(1)
            
            level += 1
            if level >= int(self.grey_bits):
                if int(self.temporal_dither) > 0:
                    dither = 0 - dither
                level = 0
            
            if level == 0:
                self.sync_lock.release()

            frame_time = time.ticks_add(frame_time, self.frame_period)
            time.sleep_us(time.ticks_diff(frame_time, time.ticks_us()))


        dc(0)
        cs(0)
        spi.write(bytearray([0xAE])) # display off
        cs(1)

        self._stopped = True



