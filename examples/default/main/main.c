/*
 * Draws on a 128x64 OLED with an SH1106 controller.
 *
 * The controller keeps the picture as pages: a byte is a column of eight rows,
 * bit 0 is the topmost row of the page. The example fills the buffer of the
 * display directly and sends it page by page, which is what the driver expects:
 * the rows of an area passed to esp_lcd_panel_draw_bitmap() must be multiples of
 * eight.
 *
 * The frame shows the coordinate grid: a border, lines every eight rows, ticks
 * every sixteen columns, a bar that moves along the bottom and a counter that
 * grows by one pixel per frame. Every forty frames the panel inverts the picture
 * on its way to the glass — the controller does it itself, the buffer is not
 * touched.
 */

#include <stdio.h>
#include <string.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_sh1106.h>

static const char *TAG = "example";

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

static uint8_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];

static void set_pixel(int x, int y, bool on)
{
    if (x < 0 || y < 0 || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT)
        return;

    uint8_t *byte = &framebuffer[(y / 8) * DISPLAY_WIDTH + x];
    if (on)
        *byte |= 1u << (y % 8);
    else
        *byte &= ~(1u << (y % 8));
}

static void fill_rect(int x, int y, int width, int height, bool on)
{
    for (int row = y; row < y + height; row++)
        for (int column = x; column < x + width; column++)
            set_pixel(column, row, on);
}

static void draw_grid(void)
{
    // the border
    for (int x = 0; x < DISPLAY_WIDTH; x++)
    {
        set_pixel(x, 0, true);
        set_pixel(x, DISPLAY_HEIGHT - 1, true);
    }
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
    {
        set_pixel(0, y, true);
        set_pixel(DISPLAY_WIDTH - 1, y, true);
    }

    // the pages: a line every eight rows, short ticks every sixteen columns
    for (int y = 8; y < DISPLAY_HEIGHT - 1; y += 8)
        for (int x = 1; x < DISPLAY_WIDTH - 1; x++)
            set_pixel(x, y, true);

    for (int x = 16; x < DISPLAY_WIDTH - 1; x += 16)
    {
        for (int y = 1; y < 4; y++)
            set_pixel(x, y, true);
        for (int y = DISPLAY_HEIGHT - 4; y < DISPLAY_HEIGHT - 1; y++)
            set_pixel(x, y, true);
    }
}

void app_main(void)
{
    // the I2C bus of the module
    i2c_master_bus_config_t bus_config =
    {
        .i2c_port = -1,
        .sda_io_num = CONFIG_EXAMPLE_I2C_SDA,
        .scl_io_num = CONFIG_EXAMPLE_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));

    // the panel takes a control byte with the command/data bit in bit 6
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_config =
    {
        .dev_addr = CONFIG_EXAMPLE_I2C_ADDR,
        .scl_speed_hz = CONFIG_EXAMPLE_I2C_CLOCK_HZ,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &io_config, &io));

    esp_lcd_panel_sh1106_config_t sh1106_config =
    {
        .height = DISPLAY_HEIGHT,
    };
    esp_lcd_panel_dev_config_t panel_config =
    {
        .reset_gpio_num = CONFIG_EXAMPLE_RESET_GPIO,
        .bits_per_pixel = 1,
        .vendor_config = &sh1106_config,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(io, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    bool inverted = false;

    for (unsigned frame = 0;; frame++)
    {
        memset(framebuffer, 0, sizeof(framebuffer));
        draw_grid();

        // a bar that moves along the bottom, one step per frame
        const int bar_x = (frame * 4) % (DISPLAY_WIDTH - 16);
        fill_rect(bar_x, DISPLAY_HEIGHT - 8, 8, 6, true);

        // a counter that grows by one pixel per frame and starts over
        const int counter = frame % (DISPLAY_WIDTH - 16);
        fill_rect(4, DISPLAY_HEIGHT - 24, counter, 6, true);

        for (int y = 0; y < DISPLAY_HEIGHT; y += 8)
            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, DISPLAY_WIDTH, y + 8,
                                                      framebuffer + (y / 8) * DISPLAY_WIDTH));

        // the controller can invert the picture on its way to the glass, the
        // buffer stays as it is
        if (frame % 40 == 39)
        {
            inverted = !inverted;
            ESP_LOGI(TAG, "The panel is %s", inverted ? "inverted" : "back to normal");
            ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, inverted));
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
