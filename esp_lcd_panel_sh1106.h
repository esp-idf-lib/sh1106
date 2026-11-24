/*
 * SH1106 panel driver for the esp_lcd framework.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SH1106 configuration structure
 *
 * To be used as esp_lcd_panel_dev_config_t.vendor_config.
 * See esp_lcd_new_panel_sh1106().
 */
typedef struct
{
    /**
     * @brief Display height in pixels (64 or 32)
     *
     * The default is 64.
     */
    uint8_t height;
    /**
     * @brief Display contrast, 0..255
     *
     * The default is 128.
     */
    uint8_t contrast;
    /**
     * @brief Column address offset
     *
     * The controller has 132 columns of display memory and a module of 128
     * pixels uses the columns 2..129, so the first pixel of a line lands in the
     * column 2. The default is 2, which suits the common 128x64 modules.
     */
    uint8_t column_offset;
} esp_lcd_panel_sh1106_config_t;

/**
 * @brief Create an LCD panel for the SH1106 controller
 *
 * @param[in] io LCD panel IO handle
 * @param[in] panel_dev_config general panel device configuration
 * @param[out] ret_panel returned LCD panel handle
 * @return
 *      - ESP_OK                on success
 *      - ESP_ERR_INVALID_ARG   if the arguments are invalid
 *      - ESP_ERR_NO_MEM        if out of memory
 *
 * @note The default panel size is 128x64. Use esp_lcd_panel_sh1106_config_t to
 *       set another size, the contrast or the column offset.
 * @note The rows of an area passed to esp_lcd_panel_draw_bitmap() must be page
 *       aligned: both the first and the last row plus one have to be multiples
 *       of 8. The memory of the controller cannot be read back, so an area that
 *       starts or ends inside a page cannot be merged with what is already
 *       there.
 * @note esp_lcd_panel_swap_xy() returns ESP_ERR_NOT_SUPPORTED: the data is sent
 *       page by page and an exchange of the axes would need a transposed
 *       buffer.
 *
 * Example usage:
 * @code {c}
 * esp_lcd_panel_sh1106_config_t sh1106_config = {
 *     .height = 64,
 * };
 * esp_lcd_panel_dev_config_t panel_config = {
 *     .reset_gpio_num = -1,
 *     .bits_per_pixel = 1,
 *     .vendor_config = &sh1106_config,
 * };
 * esp_lcd_panel_handle_t panel = NULL;
 * esp_lcd_new_panel_sh1106(io, &panel_config, &panel);
 * @endcode
 */
esp_err_t esp_lcd_new_panel_sh1106(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
