#pragma once
#include <cstdint>

namespace cr {

/* Claude session state shown as the big status ring. */
enum ClaudeStatus : uint8_t {
    ST_BOOT = 0,   /* blue   — starting up */
    ST_NO_KBD,     /* grey   — waiting for the USB keyboard */
    ST_READY,      /* white  — keyboard up, idle */
    ST_RUNNING,    /* green  — Claude running/thinking */
    ST_QUESTION,   /* amber  — Claude is asking; knob selects */
    ST_STOPPED,    /* red    — Claude stopped */
};

void statusUiCreate();
void statusUiSetStatus(ClaudeStatus st);
/* transient line at the bottom: last key/knob event (debug + feedback) */
void statusUiSetEvent(const char *text);

} // namespace cr
