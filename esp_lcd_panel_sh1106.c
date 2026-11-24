/*
 * SH1106 panel driver for the esp_lcd framework.
 *
 * The controller is the one that sits on most 128x64 modules, including the
 * "1.3 inch" boards that come with a rotary encoder. It looks like an SSD1306
 * and is wired the same way, but it is not a clone: it has 132 columns of
 * display memory and no horizontal addressing mode, so the SSD1306 driver
 * cannot talk to it. Here the 128 pixels of a line are placed at columns 2..129,
 * the offset the datasheet calls the column address offset, and the data of a
 * page is sent page by page.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_sh1106.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "lcd_panel.sh1106";

// SH1106 commands
#define SH1106_CMD_SET_COLUMN_LOW         0x00
#define SH1106_CMD_SET_COLUMN_HIGH        0x10
#define SH1106_CMD_SET_PAGE               0xB0
#define SH1106_CMD_SET_CHARGE_PUMP        0xAD
#define SH1106_CMD_SET_CHARGE_PUMP_ON     0x8B
#define SH1106_CMD_SET_PUMP_VOLTAGE       0x33
#define SH1106_CMD_MIRROR_X_OFF           0xA0
#define SH1106_CMD_MIRROR_X_ON            0xA1
#define SH1106_CMD_INVERT_OFF             0xA6
#define SH1106_CMD_INVERT_ON              0xA7
#define SH1106_CMD_SET_MULTIPLEX          0xA8
#define SH1106_CMD_DISP_OFF              0xAE
#define SH1106_CMD_DISP_ON                0xAF
#define SH1106_CMD_MIRROR_Y_OFF           0xC0
#define SH1106_CMD_MIRROR_Y_ON            0xC8
#define SH1106_CMD_SET_DISPLAY_OFFSET     0xD3
#define SH1106_CMD_SET_CLOCK_DIV          0xD5
#define SH1106_CMD_SET_PRECHARGE          0xD9
#define SH1106_CMD_SET_COMPINS            0xDA
#define SH1106_CMD_SET_VCOM_DESELECT      0xDB
#define SH1106_CMD_SET_START_LINE         0x40
#define SH1106_CMD_SET_CONTRAST           0x81
#define SH1106_CMD_SET_PAGE_ADDRESS_MODE  0x20
#define SH1106_CMD_ENTIRE_DISPLAY_OFF     0xA4

typedef struct
{
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    uint8_t height;
    uint8_t contrast;
    uint8_t column_offset;
    gpio_num_t reset_gpio_num;
    int x_gap;
    int y_gap;
    bool reset_level;
} sh1106_panel_t;

static esp_err_t panel_sh1106_del(esp_lcd_panel_t *panel);
static esp_err_t panel_sh1106_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_sh1106_init(esp_lcd_panel_t *panel);
static esp_err_t panel_sh1106_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_sh1106_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_sh1106_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_sh1106_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_sh1106_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_sh1106_disp_on_off(esp_lcd_panel_t *panel, bool on_off);

esp_err_t esp_lcd_new_panel_sh1106(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(panel_dev_config->bits_per_pixel == 1, ESP_ERR_INVALID_ARG, TAG, "bpp must be 1");

    const esp_lcd_panel_sh1106_config_t *spec = (const esp_lcd_panel_sh1106_config_t *)panel_dev_config->vendor_config;

    sh1106_panel_t *sh1106 = calloc(1, sizeof(sh1106_panel_t));
    ESP_RETURN_ON_FALSE(sh1106, ESP_ERR_NO_MEM, TAG, "no mem for sh1106 panel");

    if (panel_dev_config->reset_gpio_num >= 0)
    {
        gpio_config_t io_conf =
        {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "configure GPIO for RST line failed");
            free(sh1106);
            return err;
        }
    }

    sh1106->io = io;
    sh1106->reset_gpio_num = panel_dev_config->reset_gpio_num;
    sh1106->reset_level = panel_dev_config->flags.reset_active_high;
    sh1106->height = (spec && spec->height) ? spec->height : 64;
    sh1106->contrast = (spec && spec->contrast) ? spec->contrast : 128;
    // 132 columns of memory against 128 pixels of a line
    sh1106->column_offset = (spec && spec->column_offset) ? spec->column_offset : 2;

    sh1106->base.del = panel_sh1106_del;
    sh1106->base.reset = panel_sh1106_reset;
    sh1106->base.init = panel_sh1106_init;
    sh1106->base.draw_bitmap = panel_sh1106_draw_bitmap;
    sh1106->base.invert_color = panel_sh1106_invert_color;
    sh1106->base.set_gap = panel_sh1106_set_gap;
    sh1106->base.mirror = panel_sh1106_mirror;
    sh1106->base.swap_xy = panel_sh1106_swap_xy;
    sh1106->base.disp_on_off = panel_sh1106_disp_on_off;
    *ret_panel = &(sh1106->base);

    ESP_LOGD(TAG, "new sh1106 panel @%p", sh1106);

    return ESP_OK;
}

static esp_err_t panel_sh1106_del(esp_lcd_panel_t *panel)
{
    sh1106_panel_t *sh1106 = __containerof(panel, sh1106_panel_t, base);

    if (sh1106->reset_gpio_num >= 0)
        gpio_reset_pin(sh1106->reset_gpio_num);

    ESP_LOGD(TAG, "del sh1106 panel @%p", sh1106);
    free(sh1106);

    return ESP_OK;
}

static esp_err_t panel_sh1106_reset(esp_lcd_panel_t *panel)
{
    sh1106_panel_t *sh1106 = __containerof(panel, sh1106_panel_t, base);

    // the module may have no reset line, the controller is reset by the
    // power-on sequence then
    if (sh1106->reset_gpio_num >= 0)
    {
        gpio_set_level(sh1106->reset_gpio_num, sh1106->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(sh1106->reset_gpio_num, !sh1106->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_OK;
}

static esp_err_t panel_sh1106_init(esp_lcd_panel_t *panel)
{
    sh1106_panel_t *sh1106 = __containerof(panel, sh1106_panel_t, base);
    esp_lcd_panel_io_handle_t io = sh1106->io;

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_DISP_OFF, NULL, 0), TAG, "display off failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_CLOCK_DIV, (uint8_t[])
    {
        0x80
    }, 1), TAG,
    "clock divider failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_MULTIPLEX, (uint8_t[])
    {
        sh1106->height - 1
    }, 1),
    TAG, "multiplex ratio failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_DISPLAY_OFFSET, (uint8_t[])
    {
        0x00
    }, 1), TAG,
    "display offset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_START_LINE | 0x00, NULL, 0), TAG,
                        "start line failed");
    // the charge pump is inside the controller and feeds the panel from VDD
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_CHARGE_PUMP, (uint8_t[])
    {
        SH1106_CMD_SET_CHARGE_PUMP_ON
    }, 1), TAG, "charge pump failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_PUMP_VOLTAGE, (uint8_t[])
    {
        0x33
    }, 1), TAG,
    "pump voltage failed");
    // the modules are wired so that the segment remap and the reverse scan
    // direction give the upright picture, the plain directions turn it upside
    // down; esp_lcd_panel_mirror() swaps them back
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_MIRROR_X_ON, NULL, 0), TAG, "mirror x failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_MIRROR_Y_ON, NULL, 0), TAG, "mirror y failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_COMPINS, (uint8_t[])
    {
        sh1106->height == 64 ? 0x12 : 0x02
    }, 1), TAG, "com pins failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_CONTRAST, (uint8_t[])
    {
        sh1106->contrast
    }, 1),
    TAG, "contrast failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_PRECHARGE, (uint8_t[])
    {
        0x22
    }, 1), TAG,
    "precharge failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_VCOM_DESELECT, (uint8_t[])
    {
        0x35
    }, 1), TAG,
    "vcom deselect failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_ENTIRE_DISPLAY_OFF, NULL, 0), TAG,
                        "entire display off failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_INVERT_OFF, NULL, 0), TAG, "invert off failed");
    // the controller has no horizontal addressing mode, only the page mode; the
    // command is sent so that a controller that was left in another mode comes
    // back to it after a software restart
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_PAGE_ADDRESS_MODE, (uint8_t[])
    {
        0x02
    }, 1), TAG,
    "page addressing mode failed");

    return ESP_OK;
}

static esp_err_t panel_sh1106_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                          const void *color_data)
{
    sh1106_panel_t *sh1106 = __containerof(panel, sh1106_panel_t, base);
    esp_lcd_panel_io_handle_t io = sh1106->io;

    ESP_RETURN_ON_FALSE(color_data, ESP_ERR_INVALID_ARG, TAG, "no data");
    ESP_RETURN_ON_FALSE(x_start >= 0 && x_end > x_start && y_start >= 0 && y_end > y_start, ESP_ERR_INVALID_ARG, TAG,
                        "invalid area");
    // a page is the unit of the memory: eight rows share a byte, and the
    // controller cannot read the memory back, so an area that starts or ends
    // inside a page cannot be merged with what is already there
    ESP_RETURN_ON_FALSE((y_start % 8) == 0 && (y_end % 8) == 0, ESP_ERR_INVALID_ARG, TAG, "rows must be page aligned");

    x_start += sh1106->x_gap;
    x_end += sh1106->x_gap;
    y_start += sh1106->y_gap;
    y_end += sh1106->y_gap;

    const int width = x_end - x_start;
    const uint8_t *src = color_data;

    for (int y = y_start; y < y_end; y += 8)
    {
        const int column = x_start + sh1106->column_offset;

        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_PAGE | ((y / 8) & 0x0F), NULL, 0), TAG,
                            "set page failed");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_COLUMN_LOW | (column & 0x0F), NULL, 0), TAG,
                            "set column low failed");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SH1106_CMD_SET_COLUMN_HIGH | ((column >> 4) & 0x0F), NULL, 0),
                            TAG, "set column high failed");

        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(io, -1, src, width), TAG, "send the page failed");
        src += width;
    }

    return ESP_OK;
}

static esp_err_t panel_sh1106_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    sh1106_panel_t *sh1106 = __containerof(panel, sh1106_panel_t, base);
    esp_lcd_panel_io_handle_t io = sh1106->io;

    int command = invert_color_data ? SH1106_CMD_INVERT_ON : SH1106_CMD_INVERT_OFF;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "invert failed");

    return ESP_OK;
}

static esp_err_t panel_sh1106_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    sh1106_panel_t *sh1106 = __containerof(panel, sh1106_panel_t, base);
    esp_lcd_panel_io_handle_t io = sh1106->io;

    // the upright picture of a module is the remapped one, so a mirror is the
    // plain direction
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, mirror_x ? SH1106_CMD_MIRROR_X_OFF : SH1106_CMD_MIRROR_X_ON, NULL, 0),
                        TAG, "mirror x failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, mirror_y ? SH1106_CMD_MIRROR_Y_OFF : SH1106_CMD_MIRROR_Y_ON, NULL, 0),
                        TAG, "mirror y failed");

    return ESP_OK;
}

static esp_err_t panel_sh1106_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    // the data of the memory is sent page by page and the pages run along the
    // columns, so an exchange of the axes would need a transposed buffer, which
    // the interface does not allow to prepare here
    ESP_RETURN_ON_FALSE(!swap_axes, ESP_ERR_NOT_SUPPORTED, TAG, "swap_xy is not supported by the SH1106 controller");

    return ESP_OK;
}

static esp_err_t panel_sh1106_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    sh1106_panel_t *sh1106 = __containerof(panel, sh1106_panel_t, base);

    sh1106->x_gap = x_gap;
    sh1106->y_gap = y_gap;

    return ESP_OK;
}

static esp_err_t panel_sh1106_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    sh1106_panel_t *sh1106 = __containerof(panel, sh1106_panel_t, base);
    esp_lcd_panel_io_handle_t io = sh1106->io;

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, on_off ? SH1106_CMD_DISP_ON : SH1106_CMD_DISP_OFF, NULL, 0), TAG,
                        "display on/off failed");
    // the panel needs about 100 ms to settle after the command
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}
