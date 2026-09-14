/****************************************************************************
 * apps/examples/camera_diag/camera_diag_tflm_init.h
 ****************************************************************************/

#ifndef APPS_EXAMPLES_CAMERA_DIAG_CAMERA_DIAG_TFLM_INIT_H
#define APPS_EXAMPLES_CAMERA_DIAG_CAMERA_DIAG_TFLM_INIT_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

int camera_diag_tflm_init_diag(void);

int camera_diag_tflm_nms_selftest(void);

/* Returns -1 until the current preprocess/invoke transaction decodes output. */
int camera_diag_tflm_last_nms_count(void);

/* Returns only accepted stain, damage, and wrinkle candidates. */
int camera_diag_tflm_last_defect_nms_count(void);

/* Returns the highest-confidence accepted energy level (1..5), or -1. */
int camera_diag_tflm_last_energy_level(void);
bool camera_diag_tflm_last_label_detected(void);

int camera_diag_tflm_preprocess_diag(const uint8_t *rgb565, size_t frame_len,
                                     int frame_width, int frame_height);

#ifdef __cplusplus
}
#endif

#endif /* APPS_EXAMPLES_CAMERA_DIAG_CAMERA_DIAG_TFLM_INIT_H */
