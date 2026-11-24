# SH1106

[![Build](https://github.com/esp-idf-lib/sh1106/actions/workflows/build.yml/badge.svg)](https://github.com/esp-idf-lib/sh1106/actions/workflows/build.yml)
[![Build docs](https://github.com/esp-idf-lib/sh1106/actions/workflows/build-docs.yml/badge.svg)](https://github.com/esp-idf-lib/sh1106/actions/workflows/build-docs.yml)
[![Validation](https://github.com/esp-idf-lib/sh1106/actions/workflows/validate-component.yml/badge.svg)](https://github.com/esp-idf-lib/sh1106/actions/workflows/validate-component.yml)
[![ESP Component Registry](https://img.shields.io/badge/component%20registry-sh1106-blue)](https://components.espressif.com/components/esp-idf-lib/sh1106)

* [Documentation](https://esp-idf-lib.github.io/sh1106/)
* [Repository](https://github.com/esp-idf-lib/sh1106)
* [Issues](https://github.com/esp-idf-lib/sh1106/issues)
* [Discussions](https://github.com/esp-idf-lib/core/discussions)
* [Components Registry](https://components.espressif.com/components/esp-idf-lib/sh1106)

`esp_lcd` panel driver for the SH1106 OLED controller, the chip that sits on most
128x64 modules, including the "1.3 inch" boards that come with a rotary encoder.

## Why a separate driver

The SH1106 looks like an SSD1306: same I2C address, same control byte, same
page-major memory, same commands for contrast, inversion and the display switch.
It is not a clone, though, and the SSD1306 driver cannot drive it:

* the controller has **132 columns of display memory** and a module of 128 pixels
  uses the columns 2..129, so every line has to be shifted by two columns;
* it has **no horizontal addressing mode** — there are no window commands `0x21`
  and `0x22`, the only way to write is to select a page and a column and then
  send the bytes of the page.

A picture written through the SSD1306 driver lands in the wrong place, the rest
of the memory keeps whatever was there, and the glass shows noise with a readable
stripe where the data happened to hit.

## Features

* implements `esp_lcd_panel_interface_t`, so it plugs into the usual
  `esp_lcd_panel_ops.h` calls and works with any `esp_lcd` panel IO — I2C and SPI
* writes the memory page by page with the column offset of the controller
* charge pump, contrast, inversion, segment remap and COM scan direction
* checks the arguments and reports the codes of the calls it makes

## Usage

```c
#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_sh1106.h>

i2c_master_bus_handle_t bus;
i2c_master_bus_config_t bus_config = {
    .i2c_port = -1,
    .sda_io_num = GPIO_NUM_22,
    .scl_io_num = GPIO_NUM_23,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};
ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));

esp_lcd_panel_io_handle_t io;
esp_lcd_panel_io_i2c_config_t io_config = {
    .dev_addr = 0x3C,
    .scl_speed_hz = 400000,
    .control_phase_bytes = 1,
    .dc_bit_offset = 6,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
};
ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &io_config, &io));

esp_lcd_panel_sh1106_config_t sh1106_config = {
    .height = 64,
};
esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = -1,
    .bits_per_pixel = 1,
    .vendor_config = &sh1106_config,
};
esp_lcd_panel_handle_t panel;
ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(io, &panel_config, &panel));
ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

// the memory is page-major: a byte is a column of eight rows, bit 0 on top
uint8_t framebuffer[128 * 64 / 8];
for (int y = 0; y < 64; y += 8)
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, 128, y + 8,
                                              framebuffer + (y / 8) * 128));
```

## Notes

* **Rows must be page aligned** when drawing: both the first row and the row
  after the last one must be multiples of 8. The controller cannot read its
  memory back, so an area that starts or ends inside a page cannot be merged
  with what is already there; the driver returns `ESP_ERR_INVALID_ARG` instead
  of corrupting the neighbouring rows.
* **The upright orientation is the remapped one**: the modules are wired so that
  the segment remap and the reverse COM scan direction give the picture as it is
  drawn. `init()` sends them, and `esp_lcd_panel_mirror()` flips the image
  relative to that.
* `esp_lcd_panel_swap_xy()` returns `ESP_ERR_NOT_SUPPORTED`: the memory is sent
  page by page and an exchange of the axes would need a transposed buffer, which
  the interface does not allow to prepare in the call.
* The column offset is a part of `esp_lcd_panel_sh1106_config_t`; the default of
  2 suits the common 128x64 modules. A module that starts the picture at another
  column can be adjusted without rebuilding the driver.

## Installation

```sh
idf.py add-dependency "esp-idf-lib/sh1106"
```

## Support

* [GitHub Issues](https://github.com/esp-idf-lib/sh1106/issues)
* [Discussions](https://github.com/esp-idf-lib/core/discussions)

## Contributing

See [CONTRIBUTING.md](https://github.com/esp-idf-lib/core/blob/main/CONTRIBUTING.md).
