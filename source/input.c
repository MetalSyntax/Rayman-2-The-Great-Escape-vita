/**
 * @file input.c
 * @brief Input subsystem mapping PS Vita front touch and buttons to iOS touches.
 * @details Captures front capacitive multitouch and analog stick / button state.
 *          Maps hardware coordinates into the guest game's virtual coordinate space.
 */
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <string.h>

#include "gles.h"
#include "input.h"
#include "log.h"
#include "macho.h"

touch_point touches[SCE_TOUCH_MAX_REPORT];
int touch_count = 0;
SceCtrlData pad;

static touch_point prev_touches[SCE_TOUCH_MAX_REPORT];
static int prev_count = 0;

void input_init(void) {
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    memset(touches, 0, sizeof(touches));
    memset(prev_touches, 0, sizeof(prev_touches));
    touch_count = 0;
    prev_count = 0;
    l_info("Input subsystem initialized (front touch + analog sticks).");
}

void input_poll(void) {
    sceCtrlPeekBufferPositive(0, &pad, 1);

    SceTouchData t;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) < 1)
        t.reportNum = 0;

    memcpy(prev_touches, touches, sizeof(touches));
    prev_count = touch_count;

    touch_count = (int)t.reportNum;
    if (touch_count > SCE_TOUCH_MAX_REPORT)
        touch_count = SCE_TOUCH_MAX_REPORT;

    display_mode_t mode = gles_get_display_mode();

    for (int i = 0; i < touch_count; i++) {
        touches[i].id = t.report[i].id;
        touches[i].x = t.report[i].x / 2;
        touches[i].y = t.report[i].y / 2;

        // Landscape blit (gles.c): screen x spans the FBO's y (0..480), screen y its x reversed.
        int dst_w = mode == DISPLAY_MODE_ROTATED_FULL ? 960 : 816, dst_x = (960 - dst_w) / 2;
        int gx = (544 - touches[i].y) * 320 / 544;
        int gy = (touches[i].x - dst_x) * 480 / dst_w;

        touches[i].gx = gx < 0 ? 0 : gx > 319 ? 319 : gx;
        touches[i].gy = gy < 0 ? 0 : gy > 479 ? 479 : gy;
        touches[i].down = 1;
    }
}
