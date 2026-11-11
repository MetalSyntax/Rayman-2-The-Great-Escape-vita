/**
 * @file patch.c
 * @brief Rayman 2 specific glue: physical controls as synthetic touches on the game's own
 *        on-screen controls, controls.txt, persistent loader settings.
 * @details The iOS HUD is a set of 84-byte button structs in __DATA (evidence: _TouchpadInit at
 *          Mach-O 0x687c fills them, _UpdateButton at 0x6330 hit-tests them):
 *            +0x00 x0, +0x04 x1, +0x08 y0, +0x0c y1   (int, landscape 480x320 game coordinates)
 *            +0x1c state (1 idle, 2 pressed, 4/5 released...)
 *          _DpadButton (1 struct: virtual stick, x 10..120 y 200..310), _tp_button[7] (actions),
 *          _Dbg_tp_button[3]. Game coordinates come from -[EAGLView touches*] -> _UpdateTouch:
 *          x_game = view.y, y_game = 320 - view.x (with _isRotateLeft = 0). So a Vita button
 *          becomes a touch held at the centre of the matching rect, read at runtime (no hardcoded
 *          layout), and the left stick drags a touch inside the virtual stick rect.
 */
#include <kubridge.h>
#include <psp2/ctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gles.h"
#include "hle.h"
#include "imports.h"
#include "input.h"
#include "log.h"
#include "macho.h"
#include "patch.h"

const import_entry patch_imports[] = {{"", 0, 0}};
const int patch_imports_count = 0;

typedef struct {
    int x0, x1, y0, y1;
} rect;

#define NBTN 7
static rect *dpad, *tp_btn; // guest structs (stride 84 bytes)
static int *proj_level;    // _g_ProjectionMatrixLevel: 2 = ortho (2D) projection is current
static int deadzone = 12, stick_radius = 40;
static int hud_visible; // on-screen stick + red/blue buttons; hidden by default, L+R toggles (saved)

// Vita button -> tp_button index (-1 = unmapped). Defaults: see controls.txt template below.
static const struct {
    const char *name;
    uint32_t mask;
} vita_buttons[] = {
    {"CROSS", SCE_CTRL_CROSS}, {"CIRCLE", SCE_CTRL_CIRCLE}, {"SQUARE", SCE_CTRL_SQUARE},
    {"TRIANGLE", SCE_CTRL_TRIANGLE}, {"L", SCE_CTRL_LTRIGGER}, {"R", SCE_CTRL_RTRIGGER},
    {"START", SCE_CTRL_START},
};
#define NVB (int)(sizeof(vita_buttons) / sizeof(vita_buttons[0]))
// Layout logged on hosttest (_TouchpadInit): BTN0 x408-478 y183-253 and BTN1 x337-407 y240-310 (the two
// big bottom-right buttons), BTN3 x408-478 y110-180 (right, above BTN0), BTN2 x0-32 y70-102,
// BTN4 x0-32 y0-32 (top-left corner), BTN6 x19-67 y32-70, BTN5 = the whole screen (camera swipe area).
// Hosttest (2026-11-04, Jail level): BTN0 is the red "magic fist" button (CROSS on BTN0 fired a fist),
// BTN1 the blue one below it (jump). Vita convention: CROSS = jump, SQUARE = shoot.
static int map[NVB] = {1, 3, 0, 6, 2, -1, 4};

static const char controls_template[] =
    "# Rayman 2 (PS Vita) controls. VITA_BUTTON = BTNn (n = on-screen button index 0..6, see the\n"
    "# \"[INPUT] tp_button\" lines of the log for each button's position) or NONE.\n"
    "# Left stick / D-pad = virtual stick. Right stick = camera drag. SELECT = display mode.\n"
    "# L+R = show/hide the on-screen stick and red/blue buttons (hidden by default).\n"
    "CROSS = BTN1\nCIRCLE = BTN3\nSQUARE = BTN0\nTRIANGLE = BTN6\nL = BTN2\nR = NONE\nSTART = BTN4\n"
    "DEADZONE = 12\nSTICK_RADIUS = 40\n";

static void load_controls(void) {
    FILE *f = fopen(DATA_PATH "controls.txt", "r");
    if (!f) {
        if ((f = fopen(DATA_PATH "controls.txt", "w"))) {
            fputs(controls_template, f);
            fclose(f);
        }
        l_info("[INPUT] controls.txt missing: defaults (template written)");
        return;
    }
    char line[128], key[32], val[32];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || sscanf(line, " %31[A-Za-z_] = %31s", key, val) != 2)
            continue;
        if (!strcmp(key, "DEADZONE"))
            deadzone = atoi(val);
        else if (!strcmp(key, "STICK_RADIUS"))
            stick_radius = atoi(val);
        for (int i = 0; i < NVB; i++)
            if (!strcmp(key, vita_buttons[i].name))
                map[i] = !strncmp(val, "BTN", 3) ? atoi(val + 3) : -1;
    }
    fclose(f);
    for (int i = 0; i < NVB; i++)
        if (map[i] >= NBTN)
            map[i] = -1;
    l_info("[INPUT] controls.txt: deadzone %d, stick radius %d, CROSS->BTN%d SQUARE->BTN%d", deadzone,
           stick_radius, map[0], map[2]);
}

// Loader settings persisted in saves/ (Phase 12): display mode toggled with SELECT.
static void load_settings(void) {
    FILE *f = fopen(DOCS_PATH "loader_settings.txt", "r");
    int mode = 0;
    if (f) {
        if (fscanf(f, "DISPLAY = %d HUD = %d", &mode, &hud_visible) < 1)
            mode = 0;
        fclose(f);
    }
    gles_set_display_mode(mode == 1 ? DISPLAY_MODE_ROTATED_FULL : DISPLAY_MODE_ROTATED_ASPECT);
}
static void save_settings(void) {
    FILE *f = fopen(DOCS_PATH "loader_settings.txt", "w");
    if (f) {
        fprintf(f, "DISPLAY = %d\nHUD = %d\n", (int)gles_get_display_mode(), hud_visible);
        fclose(f);
    }
}

// Writes one guest code word after checking the original (wrong binary/version = no patch).
static void patch_word(uint32_t va, uint32_t expect, uint32_t val) {
    uint32_t *p = (uint32_t *)(MACHO_SLIDE + va);
    if (*p != expect) {
        l_warn("[PATCH] 0x%x: found 0x%08x, expected 0x%08x: skipped", (unsigned)va, (unsigned)*p, (unsigned)expect);
        return;
    }
    kuKernelCpuUnrestrictedMemcpy(p, &val, 4);
    kuKernelFlushCaches(p, 4);
}

void patch_init(void) {
    // "MORE GAMES!" (main menu) runs the AI procedure fn_p_stLaunchIGP (0x4ee04), which only sets
    // _bRestoreIGP = 1 (`strb r2, [r3]` at 0x4ee10); the main loop then opens Gameloft's online shop,
    // made of UIKit table/tab views this loader cannot draw (hosttest: portrait switch + black screen
    // forever). The store is a nop here so the button simply does nothing.
    patch_word(0x4ee10, 0xe5c32000, 0xe1a00000);

    dpad = (rect *)macho_sym("_DpadButton");
    tp_btn = (rect *)macho_sym("_tp_button");
    proj_level = (int *)macho_sym("_g_ProjectionMatrixLevel");
    l_info("[INPUT] guest HUD: _DpadButton %p, _tp_button %p", dpad, tp_btn);
    load_controls();
    load_settings();
}

static rect *btn(int i) { return tp_btn ? (rect *)((uint8_t *)tp_btn + i * 84) : NULL; }
static int valid(const rect *r) {
    return r && r->x1 > r->x0 && r->y1 > r->y0 && r->x0 >= 0 && r->x1 <= 480 && r->y0 >= 0 && r->y1 <= 320;
}

void patch_frame(void) {
    // _TouchpadInit runs during engine init: log the HUD layout once it is filled in.
    static int logged;
    if (logged || !valid(dpad))
        return;
    logged = 1;
    l_info("[INPUT] DpadButton x %d..%d y %d..%d", dpad->x0, dpad->x1, dpad->y0, dpad->y1);
    for (int i = 0; i < NBTN; i++) {
        rect *r = btn(i);
        l_info("[INPUT] tp_button[%d] x %d..%d y %d..%d", i, r->x0, r->x1, r->y0, r->y1);
    }
}

void patch_overlay(void) {}

// The engine draws the HUD as 2D sprites in the same 480x320 space as the button rects (evidence: the
// ortho matrix of `[2D] quad` dumps maps x -> -y/160+1, y -> -x/240+1, i.e. _UpdateTouch's game coords).
// A sprite fully inside the stick or the red (BTN0) / blue (BTN1) rect, grown by 12 for the glow, is
// the button (only while the ortho projection is current: 3D quads are never hidden). +0x1c is 0 while SetGameButtonsEnable(0) (menus): nothing is hidden there.
int patch_hide_quad(float x0, float y0, float x1, float y1) {
    if (hud_visible || !valid(dpad) || !((int *)dpad)[7] || !proj_level || *proj_level != 2)
        return 0;
    const rect *rs[] = {dpad, btn(0), btn(1)};
    for (int i = 0; i < 3; i++)
        if (valid(rs[i]) && x0 >= rs[i]->x0 - 12 && x1 <= rs[i]->x1 + 12 && y0 >= rs[i]->y0 - 12 &&
            y1 <= rs[i]->y1 + 12)
            return 1;
    return 0;
}

// Game (landscape 480x320) point -> touch in the portrait 320x480 view (inverse of _UpdateTouch).
static void add_touch(int id, int gx, int gy) {
    if (touch_count >= SCE_TOUCH_MAX_REPORT)
        return;
    touch_point *t = &touches[touch_count++];
    memset(t, 0, sizeof(*t));
    t->id = id;
    t->gx = 320 - gy;
    t->gy = gx;
    t->down = 1;
}

static int axis(int v) { // -128..127 outside the dead zone, else 0
    v -= 128;
    return v > deadzone || v < -deadzone ? v : 0;
}

void patch_input(void) {
    static uint32_t prev;
    uint32_t b = pad.buttons;
    if ((b & SCE_CTRL_SELECT) && !(prev & SCE_CTRL_SELECT)) {
        gles_cycle_display_mode();
        save_settings();
    }
    const uint32_t lr = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
    if ((b & lr) == lr && (prev & lr) != lr) {
        hud_visible = !hud_visible;
        l_info("[INPUT] touch HUD %s (L+R)", hud_visible ? "shown" : "hidden");
        save_settings();
    }
    prev = b;

    // Left stick (or D-pad) -> touch dragged around the centre of the virtual stick (id 200).
    // ponytail: assumes a fixed-centre stick; if the game re-centres on touch-down this still works
    // because the first synthetic frame is always at the centre.
    static int stick_on;
    if (valid(dpad)) {
        int dx = axis(pad.lx), dy = axis(pad.ly);
        if (b & SCE_CTRL_LEFT)
            dx = -127;
        if (b & SCE_CTRL_RIGHT)
            dx = 127;
        if (b & SCE_CTRL_UP)
            dy = -127;
        if (b & SCE_CTRL_DOWN)
            dy = 127;
        int cx = (dpad->x0 + dpad->x1) / 2, cy = (dpad->y0 + dpad->y1) / 2;
        if (dx || dy) {
            add_touch(200, stick_on ? cx + dx * stick_radius / 127 : cx, stick_on ? cy + dy * stick_radius / 127 : cy);
            stick_on = 1;
        } else
            stick_on = 0;
    }

    // Right stick -> camera: a drag in the upper middle of the screen (id 201), away from the HUD.
    static int cam_on;
    int rx = axis(pad.rx), ry = axis(pad.ry);
    if (rx || ry) {
        add_touch(201, cam_on ? 240 + rx * 60 / 127 : 240, cam_on ? 110 + ry * 60 / 127 : 110);
        cam_on = 1;
    } else
        cam_on = 0;

    // Action buttons -> touch held at the centre of the mapped on-screen button (ids 210+).
    for (int i = 0; i < NVB; i++) {
        rect *r = map[i] >= 0 ? btn(map[i]) : NULL;
        if ((b & lr) == lr && (vita_buttons[i].mask & lr))
            continue; // L+R is the HUD toggle, not L's/R's button
        if ((b & vita_buttons[i].mask) && valid(r))
            add_touch(210 + i, (r->x0 + r->x1) / 2, (r->y0 + r->y1) / 2);
    }
}
