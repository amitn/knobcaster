// Async album-art fetcher. Downloads the art image over HTTPS and JPEG-decodes
// it on its OWN low-priority task so neither the UI nor the Cast net task ever
// blocks on the network or the decode. The decoded image is pushed to the UI
// via ui_set_art(); callers only ever enqueue a URL (non-blocking).
#pragma once

// Create the worker task + queue. Call once at startup.
void albumart_start(void);

// Fetch and display this art URL. Coalesced: only the latest request matters,
// so call it whenever the active track's art URL changes. Thread-safe.
void albumart_request(const char *url);

// No art for the current track (idle / no image) — hide it. Thread-safe.
void albumart_clear(void);
