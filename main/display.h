#pragma once
#include "driver/i2c_master.h"

namespace cr {

/* Battery power latch + SPD2010 display + touch + LVGL on the 1.46 board. */
bool displayInit();
i2c_master_bus_handle_t displayI2CBus();

} // namespace cr
