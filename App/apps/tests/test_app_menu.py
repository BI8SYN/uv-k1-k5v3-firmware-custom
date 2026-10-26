#!/usr/bin/env python3
"""Execute the resident menu loop with fake LCD, slots, clock and key events.

Run on Linux: python3 App/apps/tests/test_app_menu.py
Hardware debounce, screen-saver implementation and RF behavior are not tested.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root / 'App/apps/app_menu.c').read_text()
# Keep the actual slot scanning, selection, rendering and dispatch logic.
menu = source[source.index('/* Five visible apps;'):source.rindex('#endif')]
preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/keyboard.h"
#define APP_SLOT_COUNT 64u
#define APP_FLAG_COMMITTED 1u
#define APP_FLAG_EXIT_TO_MAIN 2u
#define APP_OK 0u
#define APP_CHAIN_ABORTED 1u
#define APP_NAME_LEN 16u
#define APP_VERSION_LEN 8u
#define APP_NAME_TEXT_X 21u
#define LCD_WIDTH 128u
#define UI_TEXT_SIZE 32u
#define UI_TEXT_NO_APPS 0u
#define ENABLE_FEAT_F4HWN_K5VIEWER
KEY_Code_t gKeyReading0, gKeyReading1;
typedef struct { uint8_t flags; char name[16], version[8]; uint32_t code_size; } app_header_t;
static bool installed[64], gAppVfoExit;
static uint8_t revision, gAppLastSlot;
static uint32_t ticks;
static int rendered, last_number, launched, launch_count;
static unsigned event_index;
static const char *scenario;
typedef struct { KEY_Code_t key; uint32_t at; int selected; int change; uint32_t held; } Event;
static const Event *events;
static unsigned event_count;
static void noop(void) {}
#define APP_ModalScreenSaverExit noop
#define BACKLIGHT_TurnOn noop
#define APP_WaitKeyRelease noop
#define ST7565_BlitStatusLine noop
#define ST7565_BlitFullScreen noop
#define app_status_bar noop
static void K5VIEWER_Update(bool unused) { (void)unused; }
static uint8_t APP_SlotRevision(void) { return revision; }
static uint32_t SCHEDULER_GetTick10ms(void) __attribute__((unused));
static uint32_t SCHEDULER_GetTick10ms(void) { return ticks; }
static uint8_t APP_SlotInfo(uint8_t slot, app_header_t *hdr) {
    assert(slot < 64);
    memset(hdr, 0, sizeof(*hdr));
    hdr->flags = installed[slot] ? APP_FLAG_COMMITTED : 0;
    return APP_OK;
}
static void UI_DisplayClear(void) { rendered = 0; }
static void UI_LoadText(char *s, size_t n, unsigned id) { (void)id; memset(s, 0, n); }
static void UI_PrintStringSmallNormal(const char *s, unsigned x, unsigned end, unsigned line) {
    (void)end; (void)line;
    if (x == 2) last_number = atoi(s);
}
static void app_invert_name(uint8_t line) { assert(line >= 1 && line <= 5); rendered = last_number; }
static void app_copy(char *out, uint8_t n, const char *in, uint8_t cap) {
    (void)in; (void)cap; memset(out, 0, n);
}
static void app_format_size(char out[6], uint32_t bytes) { (void)bytes; memcpy(out, "0.1KB", 6); }
static void GUI_DisplaySmallest(const char *s, uint8_t x, uint8_t y, bool a, bool b) {
    (void)s; (void)x; (void)y; (void)a; (void)b;
}
static void UI_DrawMenuKeyHints(const char *a, const char *b) { (void)a; (void)b; }
static uint8_t APP_LaunchOverlay(uint8_t slot) {
    launched = slot + 1; launch_count++; gAppLastSlot = slot; return APP_CHAIN_ABORTED;
}
static void app_show_error(const app_header_t *hdr, uint8_t rc) { (void)hdr; (void)rc; assert(false); }
static KEY_Code_t app_get_key(uint32_t *pressed_at) {
    assert(event_index < event_count);
    const Event e = events[event_index++];
    if (e.selected >= 0 && rendered != e.selected) {
        fprintf(stderr, "%s event %u: expected selected slot %d, got %d\n",
                scenario, event_index, e.selected, rendered);
        abort();
    }
    *pressed_at = e.at;
    ticks = e.at + e.held;
    if (e.change) { installed[0] = !installed[0]; revision++; }
    return e.key;
}
'''
tests = r'''
static void run(const char *name, const uint8_t *slots, unsigned count,
                const Event *keys, unsigned n) {
    scenario = name;
    memset(installed, 0, sizeof(installed));
    for (unsigned i = 0; i < count; i++) installed[slots[i] - 1] = true;
    events = keys; event_count = n; event_index = 0; launch_count = 0; launched = 0;
    APP_MenuOpen();
    assert(event_index == n);
    printf("PASS %s\n", name);
}
#define RUN(name, slots, ...) do { const Event e[] = { __VA_ARGS__ }; \
    run(name, slots, sizeof(slots), e, sizeof(e)/sizeof(e[0])); } while (0)
int main(void) {
    const uint8_t sparse[] = {1, 4, 12};
    RUN("immediate digit then pair", sparse,
        {KEY_4, 0, -1, 0}, {KEY_1, 100, 4, 0}, {KEY_2, 120, 1, 0}, {KEY_EXIT, 120, 12, 0});
    assert(!launch_count);
    RUN("invalid digit and fallback", sparse,
        {KEY_1, 0, -1, 0}, {KEY_5, 100, 1, 0}, {KEY_4, 120, 1, 0},
        {KEY_9, 140, 4, 0}, {KEY_EXIT, 140, 4, 0});
    const uint8_t slots[] = {1, 2, 3, 4, 6, 9, 10, 12, 20, 23, 64};
    RUN("timeout at one second", slots,
        {KEY_1, 0, -1, 0}, {KEY_2, 100, 1, 0}, {KEY_EXIT, 100, 2, 0});
    RUN("second key held past deadline", slots,
        {KEY_1, 0, -1, 0}, {KEY_2, 70, 1, 0, 40}, {KEY_EXIT, 110, 12, 0});
    RUN("pair just before timeout", slots,
        {KEY_1, 0, -1, 0}, {KEY_2, 99, 1, 0}, {KEY_EXIT, 99, 12, 0});
    RUN("completed pair starts a new number", slots,
        {KEY_1, 0, -1, 0}, {KEY_2, 10, 1, 0}, {KEY_3, 20, 12, 0}, {KEY_EXIT, 20, 3, 0});
    const uint8_t gap[] = {4, 12};
    RUN("missing first slot still permits pair", gap,
        {KEY_4, 0, -1, 0}, {KEY_1, 100, 4, 0}, {KEY_2, 120, 4, 0}, {KEY_EXIT, 120, 12, 0});
    RUN("zero prefix and slot 64 with scrolling", slots,
        {KEY_0, 0, -1, 0}, {KEY_1, 10, -1, 0}, {KEY_6, 20, 1, 0},
        {KEY_4, 30, 6, 0}, {KEY_EXIT, 30, 64, 0});
    RUN("zero completes tens and 00 leaves selection unchanged", slots,
        {KEY_1, 0, -1, 0}, {KEY_0, 10, 1, 0}, {KEY_0, 20, 10, 0},
        {KEY_0, 30, 10, 0}, {KEY_STAR, 35, 10, 0},
        {KEY_2, 40, 10, 0}, {KEY_0, 50, 2, 0},
        {KEY_EXIT, 50, 20, 0});
    RUN("fallback after out-of-range pair", slots,
        {KEY_6, 0, -1, 0}, {KEY_9, 10, 6, 0}, {KEY_EXIT, 10, 9, 0});
    RUN("counter rollover", slots,
        {KEY_1, UINT32_MAX - 20, -1, 0}, {KEY_2, 20, 1, 0}, {KEY_EXIT, 20, 12, 0});
    RUN("counter rollover timeout", slots,
        {KEY_1, UINT32_MAX - 100, -1, 0}, {KEY_2, 20, 1, 0}, {KEY_EXIT, 20, 2, 0});
    const KEY_Code_t interruptions[] = {KEY_UP, KEY_DOWN, KEY_STAR, KEY_INVALID, KEY_F, KEY_MENU};
    for (unsigned i = 0; i < sizeof(interruptions)/sizeof(interruptions[0]); i++) {
        Event e[] = {{KEY_1, 0, -1, 0}, {interruptions[i], 10, 1, 0},
                     {KEY_2, 20, -1, 0}, {KEY_EXIT, 20, 2, 0}};
        run("non-digit clears prefix", slots, sizeof(slots), e, 4);
        assert(launch_count == (interruptions[i] == KEY_MENU));
        if (launch_count) assert(launched == 1);
    }
    RUN("slot revision clears prefix", slots,
        {KEY_1, 0, -1, 1}, {KEY_2, 10, 2, 0}, {KEY_EXIT, 10, 2, 0});
    RUN("menu reopening clears prefix", slots, {KEY_1, 0, -1, 0}, {KEY_EXIT, 1, 1, 0});
    RUN("new menu independent digit", slots, {KEY_2, 2, 1, 0}, {KEY_EXIT, 2, 2, 0});
    RUN("select then launch with MENU", slots,
        {KEY_1, 0, -1, 0}, {KEY_2, 10, 1, 0}, {KEY_MENU, 20, 12, 0}, {KEY_EXIT, 20, 12, 0});
    assert(launch_count == 1 && launched == 12);
    const Event empty[] = {{KEY_0, 0, 0, 0}, {KEY_6, 10, 0, 0}, {KEY_4, 20, 0, 0},
                           {KEY_UP, 30, 0, 0}, {KEY_MENU, 40, 0, 0}, {KEY_EXIT, 40, 0, 0}};
    run("empty menu", NULL, 0, empty, 6);
    assert(!launch_count);
    puts("All menu scenarios passed.");
}
'''
reader = source[source.index('static KEY_Code_t app_get_key('):source.index('static void app_copy(')]
reader_preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "driver/keyboard.h"
#define APP_KEY_REPEAT_FIRST 40u
#define APP_KEY_REPEAT_NEXT 5u
static uint32_t ms = 700u;
static uint32_t SCHEDULER_GetTick10ms(void) { return ms / 10u; }
static void SYSTEM_DelayMs(uint32_t delay) { ms += delay; }
static KEY_Code_t KEYBOARD_PollFake(void) { return ms < 1100u ? KEY_2 : KEY_INVALID; }
#define KEYBOARD_Poll KEYBOARD_PollFake
#define KEYBOARD_GetKey KEYBOARD_PollFake
#define APP_IsScreenSaverDisplayed() false
#define APP_ModalScreenSaverExit() ((void)0)
#define BACKLIGHT_TurnOn() ((void)0)
#define APP_WaitKeyRelease() ((void)0)
#define APP_ModalBacklightTick(active) ((void)(active))
'''
reader_test = r'''
int main(void) {
    uint32_t pressed_at = 0;
    assert(app_get_key(&pressed_at) == KEY_2);
    assert(pressed_at == 73u);  /* Debounced at 730 ms, released at 1100 ms. */
    assert(ms == 1100u);
    puts("PASS held key records acceptance time before release");
}
'''
with tempfile.TemporaryDirectory(prefix='app-menu-test-') as directory:
    path = Path(directory)
    (path / 'reader.c').write_text(reader_preamble + reader + reader_test)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-I', str(root / 'App'), str(path / 'reader.c'),
                    '-o', str(path / 'reader')], check=True)
    subprocess.run([str(path / 'reader')], check=True)
    (path / 'menu.c').write_text(preamble + menu + tests)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-Wno-missing-field-initializers', '-fsanitize=address,undefined', '-g',
                    '-I', str(root / 'App'), str(path / 'menu.c'), '-o', str(path / 'menu')], check=True)
    subprocess.run([str(path / 'menu')], check=True)
