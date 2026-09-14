/****************************************************************************
 * apps/examples/camera_diag/esp32p4_dsi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * L1.8.25f display-link Stage 1: EK79007 MIPI-DSI panel diagnostic.
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_CAMERA_DIAG_ESP32P4_DSI_H
#define __APPS_EXAMPLES_CAMERA_DIAG_ESP32P4_DSI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: esp32p4_dsi_pattern_diag
 *
 * Description:
 *   Stage-1 DSI panel diagnostic: bring up the EK79007 panel (solid color /
 *   color bar) and hold it for hold_ms, then tear down. No camera, no
 *   image capture.
 *
 * Input Parameters:
 *   mode    - 0 = solid red, 1 = solid green, 2 = 8-color vertical bar
 *   hold_ms - display hold time in ms (0 = default 5000)
 *
 * Returned Value:
 *   OK (0) on success; negative errno on failure.
 ****************************************************************************/

int esp32p4_dsi_pattern_diag(int mode, uint32_t hold_ms);

/* Render the minimal OpenVela-compatible system status screen.  This is a
 * text-only board status UI, not a live camera inspection result. */

int esp32p4_dsi_status_ui_diag(uint32_t hold_ms);

/* Start/stop the persistent status UI.  Start returns after the DSI
 * framebuffer and DMA refresh loop are active, leaving NSH available. */

int esp32p4_dsi_status_ui_start(void);
void esp32p4_dsi_status_ui_stop(void);
bool esp32p4_dsi_status_ui_is_active(void);
int esp32p4_dsi_status_ui_set_inference_result(int candidate_count);
int esp32p4_dsi_status_ui_set_inspection_result(int candidate_count,
                                                int energy_level);
int esp32p4_dsi_status_ui_set_label_detected(bool detected);
int esp32p4_dsi_status_ui_touch_feedback(uint16_t x, uint16_t y,
                                         unsigned int event_count,
                                         bool clear_trail);
void esp32p4_dsi_status_ui_touch_release(void);

/* Workflow stage callback: called when a button is pressed and stage changes.
 * stage: 1=CAPTURE, 2=GALLERY, 3=LABEL, 4=ENERGY,
 *        5=STAIN, 6=DAMAGE, 7=WRINKLE, 8=POSITION */
typedef void (*esp32p4_dsi_workflow_cb_t)(int stage);
void esp32p4_dsi_set_workflow_callback(esp32p4_dsi_workflow_cb_t cb);

int esp32p4_dsi_show_rgb565(FAR uint8_t *frame, size_t frame_bytes,
                             uint32_t hold_ms);

/* Limited live-preview session.  The caller owns the CSI/GDMA source
 * interrupt and must forward it to esp32p4_dsi_gdma_irq_handler(). */

int esp32p4_dsi_live_start(FAR uint8_t *frame, size_t frame_bytes);
void esp32p4_dsi_live_stop(void);
void esp32p4_dsi_gdma_irq_handler(void);

#endif /* __APPS_EXAMPLES_CAMERA_DIAG_ESP32P4_DSI_H */
