# SH1106 example

Draws on a 128x64 OLED with the SH1106 controller and shows what the driver does
with the memory: a border, a line every eight rows, ticks every sixteen columns,
a bar that moves along the bottom and a counter that grows one pixel per frame.
Every forty frames the panel inverts the picture itself.

## Wiring

| Module | ESP32             | Notes                                   |
|--------|-------------------|-----------------------------------------|
| VCC    | 3.3 V             | 100 nF to GND close to the module       |
| GND    | GND               |                                         |
| SDA    | `EXAMPLE_I2C_SDA` | 4.7 kOhm pull-up to 3.3 V               |
| SCL    | `EXAMPLE_I2C_SCL` | 4.7 kOhm pull-up to 3.3 V               |
| RESET  | `EXAMPLE_RESET_GPIO` | optional, -1 when the module has none |

The address is `EXAMPLE_I2C_ADDR`, `0x3C` by default: the address pin of these
modules is tied to ground. `0x3D` is the same module with the pin tied to the
supply.

Many 1.3" modules come with a rotary encoder and a button on the same board.
The encoder and the button are not a part of this example; the pins of the
display are the four at the edge of the board.

## Build and run

```sh
idf.py menuconfig     # the pins, the address and the clock of the bus
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## What you should see

The glass shows the grid at once: a frame around the edge, horizontal lines every
eight rows, short ticks on the top and on the bottom every sixteen columns, a
small bar travelling to the right along the bottom edge and a line that grows
from the left in the middle. The log prints `The panel is inverted` and
`The panel is back to normal` every four seconds.

If the picture is shifted by two columns, the module has another column offset:
set `column_offset` in `esp_lcd_panel_sh1106_config_t` instead of the default 2.
If the picture is upside down, the module is wired the other way round and the
segment remap and the COM scan direction have to be flipped with
`esp_lcd_panel_mirror(panel, true, true)`.
