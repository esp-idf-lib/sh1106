list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/eil-cmake-utils/cmake)
include(eil_check_idf_features)
eil_check_idf_has_esp_drivers()

idf_component_register(
    SRCS esp_lcd_panel_sh1106.c
    INCLUDE_DIRS .
    REQUIRES esp_lcd
    PRIV_REQUIRES esp_driver_gpio
)

include(eil_ci)
