#include "touch_spd2010.h"

#include <cstring>
#include "esp_log.h"
#include "esp_rom_sys.h"

namespace cr {

static const char *TAG = "touch";
static i2c_master_dev_handle_t s_dev = nullptr;

static bool wr(const uint8_t *data, size_t len)
{
    bool ok = i2c_master_transmit(s_dev, data, len, 50) == ESP_OK;
    esp_rom_delay_us(200);
    return ok;
}

static bool rd(uint8_t *data, size_t len)
{
    bool ok = i2c_master_receive(s_dev, data, len, 50) == ESP_OK;
    esp_rom_delay_us(200);
    return ok;
}

static bool cmdPointMode() { const uint8_t c[4] = {0x50, 0x00, 0x00, 0x00}; return wr(c, 4); }
static bool cmdStart()     { const uint8_t c[4] = {0x46, 0x00, 0x00, 0x00}; return wr(c, 4); }
static bool cmdCpuStart()  { const uint8_t c[4] = {0x04, 0x00, 0x01, 0x00}; return wr(c, 4); }
static bool cmdClearInt()  { const uint8_t c[4] = {0x02, 0x00, 0x01, 0x00}; return wr(c, 4); }

struct TpStatus {
    bool ptExist, gesture, aux;
    bool ticInBios, ticInCpu, cpuRun;
    uint16_t readLen;
};

static bool readStatusLength(TpStatus &st)
{
    uint8_t d[4] = {0x20, 0x00};
    if (!wr(d, 2) || !rd(d, 4)) return false;
    st.ptExist = d[0] & 0x01;
    st.gesture = d[0] & 0x02;
    st.aux = d[0] & 0x08;
    st.cpuRun = (d[1] >> 3) & 1;
    st.ticInCpu = (d[1] >> 5) & 1;
    st.ticInBios = (d[1] >> 6) & 1;
    st.readLen = (uint16_t)(d[3] << 8 | d[2]);
    return true;
}

struct TpTouch {
    uint8_t num = 0;
    uint16_t x = 0, y = 0;
    uint8_t weight = 0;
};

static bool readHdp(const TpStatus &st, TpTouch &t)
{
    uint8_t d[4 + 10 * 6];
    uint8_t cmd[2] = {0x00, 0x03};
    uint16_t len = st.readLen > sizeof(d) ? sizeof(d) : st.readLen;
    if (!wr(cmd, 2) || !rd(d, len)) return false;

    uint8_t checkId = d[4];
    if (checkId <= 0x0A && st.ptExist) {
        t.num = (uint8_t)((len - 4) / 6);
        if (t.num > 0) {
            t.x = (uint16_t)(((d[7] & 0xF0) << 4) | d[5]);
            t.y = (uint16_t)(((d[7] & 0x0F) << 8) | d[6]);
            t.weight = d[8];
        }
    } else {
        t.num = 0;
    }
    return true;
}

static bool readHdpStatus(uint8_t &status, uint16_t &nextLen)
{
    uint8_t d[8] = {0xFC, 0x02};
    if (!wr(d, 2) || !rd(d, 8)) return false;
    status = d[5];
    nextLen = (uint16_t)(d[2] | d[3] << 8);
    return true;
}

static bool readHdpRemain(uint16_t len)
{
    uint8_t d[32];
    uint8_t cmd[2] = {0x00, 0x03};
    if (len > sizeof(d)) len = sizeof(d);
    return wr(cmd, 2) && rd(d, len);
}

bool touchInit(i2c_master_bus_handle_t bus)
{
    if (!bus || i2c_master_probe(bus, 0x53, 200) != ESP_OK) {
        ESP_LOGW(TAG, "SPD2010 touch not on bus");
        return false;
    }
    i2c_device_config_t cfg = {};
    cfg.device_address = 0x53;
    cfg.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) return false;
    ESP_LOGI(TAG, "SPD2010 touch up");
    return true;
}

bool touchRead(uint16_t *x, uint16_t *y)
{
    static TpTouch last;
    if (!s_dev) return false;

    TpStatus st;
    if (!readStatusLength(st)) return false;

    if (st.ticInBios) {
        cmdClearInt();
        cmdCpuStart();
    } else if (st.ticInCpu) {
        cmdPointMode();
        cmdStart();
        cmdClearInt();
    } else if (st.cpuRun && st.readLen == 0) {
        cmdClearInt();
    } else if (st.ptExist || st.gesture) {
        TpTouch t;
        if (readHdp(st, t)) last = t;
        uint8_t status;
        uint16_t nextLen;
        for (int guard = 0; guard < 4; guard++) {
            if (!readHdpStatus(status, nextLen)) break;
            if (status == 0x82) {
                cmdClearInt();
                break;
            }
            if (status == 0x00) {
                readHdpRemain(nextLen);
                continue;
            }
            break;
        }
    } else if (st.cpuRun && st.aux) {
        cmdClearInt();
    }

    bool down = last.num > 0 && last.weight > 0;
    if (down) {
        *x = last.x;
        *y = last.y;
    }
    return down;
}

} // namespace cr
