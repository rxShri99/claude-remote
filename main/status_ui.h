#pragma once
#include <cstdint>

namespace cr {

/* Claude session state shown as the remote screen's status ring. */
enum ClaudeStatus : uint8_t {
    ST_READY = 0,  /* white  — paired with the Mac, idle */
    ST_RUNNING,    /* green  — Claude turn in progress */
    ST_QUESTION,   /* amber  — Claude is asking */
    ST_STOPPED,    /* red    — Claude stopped */
    ST_THINKING,   /* purple — Claude reasoning */
    ST_WORKING,    /* teal   — Claude using tools */
};

/* Builds both screens: pairing/onboarding (shown first) and the remote. */
void statusUiCreate();

/* Switch between the onboarding screen (false) and the remote (true). */
void statusUiSetConnected(bool connected);

void statusUiSetStatus(ClaudeStatus st);

/* One-line feedback (shown on whichever screen is active). */
void statusUiSetEvent(const char *text);

/* Multi-line message area on the remote screen (transcripts + Claude replies). */
void statusUiShowResponse(const char *text);

} // namespace cr
