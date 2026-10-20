# The Cheap Yellow Display's screen and touch, for MicroPython.
#
# The pins are the same on the one-USB and the two-USB boards:
#   screen: SPI clock 14, data to the screen 13, data from the screen 12,
#           select 15, command/data 2, backlight 21 (high = on). The screen
#           resets with the board, so it has no reset pin of its own.
#   touch:  XPT2046 chip on clock 25, data in 32, data out 39, select 33, and
#           a "pen" line on 36 that goes low while the screen is pressed.
#
# Boards come with one of two screen chips: the one-USB board has an ILI9341,
# most two-USB boards an ST7789. start() asks the chip for its id and sets
# itself up for whichever answers. If yours shows the wrong colours (like a
# photo negative), set INVERT; if it shows upside down, set FLIP.
import framebuf
import time
from machine import Pin, SPI, SoftSPI

WIDTH = 320
HEIGHT = 240

CHIP = None     # None: ask the screen. Or "ILI9341" or "ST7789" to skip asking.
INVERT = None   # None: the usual setting for that chip. True or False to choose.
FLIP = False    # True turns the picture upside down.

# What each chip needs. "turn" sets the screen sideways (landscape) with the
# blue and red order this board uses; "invert" swaps every colour for its
# opposite; "mode" is the SPI clock shape the chip expects.
CHIPS = {
    "ILI9341": {"turn": 0xE8, "invert": False, "mode": 0},
    "ST7789": {"turn": 0xE8, "invert": False, "mode": 3},
}
FALLBACK = "ILI9341"   # if the screen does not answer: the original board's chip

PRESS = 400   # how hard a touch must be (the touch chip's pressure reading)

_dc = Pin(2, Pin.OUT, value=1)
_cs = Pin(15, Pin.OUT, value=1)
_light = Pin(21, Pin.OUT, value=0)
_pen = Pin(36, Pin.IN)            # low while pressed (the board pulls it up)
_touch_cs = Pin(33, Pin.OUT, value=1)
_touch = SoftSPI(baudrate=1000000, polarity=0, phase=0,
                 sck=Pin(25), mosi=Pin(32), miso=Pin(39))
_spi = None


def _bus(mode, speed):
    """Open the screen's SPI bus in one of its two clock shapes."""
    global _spi
    _spi = SPI(1, baudrate=speed, polarity=mode >> 1, phase=mode & 1,
               sck=Pin(14), mosi=Pin(13), miso=Pin(12))


def _command(code, data=b""):
    """Send one command byte, then its data bytes."""
    _cs(0)
    _dc(0)
    _spi.write(bytes([code]))
    _dc(1)
    if data:
        _spi.write(data)
    _cs(1)


def _read(code):
    """Ask for one byte of a register the chip can read back."""
    _cs(0)
    _dc(0)
    _spi.write(bytes([code]))
    _dc(1)
    value = _spi.read(1)[0]
    _cs(1)
    return value


def _ask_chip():
    """Which chip is this? Returns (name or None, the bytes it answered).

    The ST7789 answers its id registers 0xDA, 0xDB and 0xDC with 85 85 52.
    The ILI9341 shows its id register 0xD3 one byte at a time, each picked
    with command 0xD9 first, and answers ... 93 41."""
    st = [_read(0xDA), _read(0xDB), _read(0xDC)]
    if st == [0x85, 0x85, 0x52]:
        return "ST7789", st
    ili = []
    for index in (1, 2, 3):
        _command(0xD9, bytes([0x10 + index]))
        ili.append(_read(0xD3))
    if ili[1:] == [0x93, 0x41]:
        return "ILI9341", st + ili
    return None, st + ili


def start():
    """Find the chip, wake the screen, clear it and turn the light on.
    Returns a line saying what was found, for the console."""
    name, answer = CHIP, []
    if name is None:
        for mode in (0, 3):                 # the two clock shapes these chips use
            _bus(mode, 4000000)             # read slowly: reads are slower than writes
            _command(0x01)                  # software reset
            time.sleep_ms(150)
            name, answer = _ask_chip()
            if name:
                break
    found = name or FALLBACK
    chip = CHIPS[found]
    _bus(chip["mode"], 40000000)
    _command(0x01)                          # software reset
    time.sleep_ms(150)
    _command(0x11)                          # wake up
    time.sleep_ms(120)
    _command(0x3A, b"\x55")                 # 16 bits per pixel
    turn = chip["turn"] ^ (0xC0 if FLIP else 0)
    _command(0x36, bytes([turn]))           # sideways, right way up
    invert = chip["invert"] if INVERT is None else INVERT
    _command(0x21 if invert else 0x20)      # colours inverted or not
    _command(0x29)                          # screen on
    fill(0, 0, WIDTH, HEIGHT, rgb(0, 0, 0))
    backlight(True)
    _touch_read(0xD0)                       # lets the pen line work
    raw = " ".join("%02x" % b for b in answer)
    if name and CHIP is None:
        return "screen: {} (answered {})".format(found, raw)
    if CHIP:
        return "screen: {} (set in screen.py)".format(found)
    return "screen: no answer ({}), using {}".format(raw, found)


def backlight(on):
    _light(1 if on else 0)


def rgb(red, green, blue):
    """A colour as the two bytes the screen takes (5 bits red, 6 green, 5 blue)."""
    value = (red & 0xF8) << 8 | (green & 0xFC) << 3 | blue >> 3
    return bytes([value >> 8, value & 0xFF])


def _area(x, y, w, h):
    """Choose the rectangle the next pixels go into, and start sending."""
    _command(0x2A, bytes([x >> 8, x & 0xFF, (x + w - 1) >> 8, (x + w - 1) & 0xFF]))
    _command(0x2B, bytes([y >> 8, y & 0xFF, (y + h - 1) >> 8, (y + h - 1) & 0xFF]))
    _cs(0)
    _dc(0)
    _spi.write(b"\x2c")                     # pixels follow
    _dc(1)


def fill(x, y, w, h, colour):
    """Paint a rectangle one colour."""
    if w <= 0 or h <= 0:
        return
    line = colour * w
    _area(x, y, w, h)
    for _ in range(h):
        _spi.write(line)
    _cs(1)


def text(x, y, words, size, colour, background):
    """Write words at (x, y) in MicroPython's own 8x8 letters, each letter
    8 x size pixels square. Letters it has no shape for show as '?'."""
    words = "".join(c if " " <= c <= "~" else "?" for c in words)
    n = len(words)
    if n == 0:
        return
    letters = framebuf.FrameBuffer(bytearray(n * 8), n * 8, 8, framebuf.MONO_HLSB)
    letters.text(words, 0, 0, 1)
    on = colour * size
    off = background * size
    row = bytearray(n * 8 * size * 2)
    step = size * 2
    _area(x, y, n * 8 * size, 8 * size)
    for r in range(8):                      # each row of the 8x8 letters...
        i = 0
        for c in range(n * 8):              # ...widened to size pixels each
            row[i:i + step] = on if letters.pixel(c, r) else off
            i += step
        for _ in range(size):               # ...and repeated size times
            _spi.write(row)
    _cs(1)


def _touch_read(command):
    """One 12-bit reading from the touch chip."""
    _touch_cs(0)
    _touch.write(bytes([command]))
    data = _touch.read(2)
    _touch_cs(1)
    return ((data[0] << 8) | data[1]) >> 3


def touched():
    """True while a finger presses the screen.

    The pen line alone is not enough: on this chip, pin 36 can dip for a
    moment when the radio is busy. So a press also has to show pressure."""
    if _pen.value():
        return False
    z1 = _touch_read(0xB0)
    z2 = _touch_read(0xC0)
    return z1 + 4095 - z2 > PRESS
