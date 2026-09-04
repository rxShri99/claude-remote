#pragma once
#include <cstdint>
#include "driver/i2c_master.h"

namespace cr {

/*
 * SPD2010 touch, ported from espressif/esp_lcd_touch_spd2010 to the new
 * i2c_master API (the component's panel-io layer sends zero-length register
 * writes the new driver rejects — the protocol itself is fine).
 */
bool touchInit(i2c_master_bus_handle_t bus);

/* Poll once. Returns true while a finger is down; x/y in panel coords. */
bool touchRead(uint16_t *x, uint16_t *y);

} // namespace cr
