/**
 * @file gles.h
 * @brief OpenGL ES 1.1 / 2.0 graphics pipeline over vitaGL with SoftFP calling convention.
 */
#pragma once
#include <stdint.h>

// Rayman 2 renders landscape into the 320x480 portrait EAGL renderbuffer (UIInterfaceOrientation 3,
// _isRotateLeft = 0): the blit always rotates it back to landscape.
typedef enum {
    DISPLAY_MODE_ROTATED_ASPECT = 0, /**< 480x320 -> 816x544 centered, aspect preserved [Default]. */
    DISPLAY_MODE_ROTATED_FULL = 1    /**< 480x320 -> 960x544 stretched. */
} display_mode_t;

/**
 * @brief Initializes the OpenGL ES pipeline and off-screen virtual FBO.
 */
void gles_init(void);

/**
 * @brief Presents current frame: blits the portrait iOS FBO rotated to landscape on the 960x544 screen.
 */
void gles_present(void);

/**
 * @brief Returns the total number of draw calls dispatched this session.
 */
uint32_t gles_get_draw_calls(void);

/**
 * @brief Marks the currently bound renderbuffer as the CAEAGLLayer drawable
 *        ([EAGLContext renderbufferStorage:fromDrawable:]).
 */
void gles_bind_drawable(void);

/**
 * @brief Gets current display presentation mode.
 */
display_mode_t gles_get_display_mode(void);

/**
 * @brief Sets current display presentation mode.
 */
void gles_set_display_mode(display_mode_t mode);

/**
 * @brief Cycles through available display presentation modes.
 */
void gles_cycle_display_mode(void);

