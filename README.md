## Greyscale driver for SSD1306 displays.

*Very specifically, for RP2040 devices with a 72x40 SPI display.*

This is a micropython native module which rapidly updates a monochrome display to achieve a greyscale image. It provides a normal greyscale framebuffer supporting all the usual primitive operations, and uses the RP2040's second core handle re-rendering and updating the display.


I created it for a tiny computer in a brick (https://www.youtube.com/watch?v=0pUV_3qeHog), but it also works on the Thumby.

If you don't want to build it yourself, you just need to copy `ugrey.mpy` to your device, and import it.

```
import ugrey

with ugrey.Device() as display:
    display.start()

    fb = display.framebuffer

    while True:
        fb.fill(0)

        # normal framebuf drawing shenanigans

        display.show()
```

After initialising the device (but before starting it), you can configure some settings:


`display.grey_bits` is the number of bits of greyscale. 1 is monochrome, 3 is probably as high as it's worth going.

`display.dither_bits` can be `0` or `1` - whether to add dithering into the mix.

`display.temporal_dither` - whether to alternate the dithering each frame

You can also initialise it with `ugrey.Device(bpp=x)`, and let it choose sensible defaults for the given bit depth. The default is `4`, which gives you 3 bits of grey + 1 bit of dither in a `GS4` framebuffer. `bpp=8` keeps the same output settings but gives you a `GS8` framebuffer.

If you don't supply a framebuffer, it will create its own using a format which fits the requested bit depth.

Depending on your display you might need to fiddle with `display.frame_period` - SSD1306 displays should work at around 5500 uS. I've had some which can't lock at this frequency, but work at 6400.
