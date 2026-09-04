#pragma once

namespace cr {

/*
 * Onboard I2S MEMS mic (BCK=15, WS=2, DIN=39) -> 16kHz mono -> IMA-ADPCM
 * blocks -> BLE audio notifications for the Mac helper.
 */
bool micStreamStart();
void micStreamStop(bool send); /* send=false -> cancel marker */
bool micStreamActive();

} // namespace cr
