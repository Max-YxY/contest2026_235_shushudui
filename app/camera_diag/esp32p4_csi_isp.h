/****************************************************************************
 * apps/examples/camera_diag/esp32p4_csi_isp.h
 *
 * L1.8.25e Stage B: NuttX camera ISP integration.
 * SC2336 RAW8 -> MIPI CSI (RAW8 bypass) -> ISP processor (RAW8 -> RGB565
 * demosaic) -> PSRAM RGB565 frame buffer.
 *
 * Build-level integration only (Stage B scope). On-device capture/flash
 * requires separate authorization.
 ****************************************************************************/

#ifndef APPS_EXAMPLES_CAMERA_DIAG_ESP32P4_CSI_ISP_H
#define APPS_EXAMPLES_CAMERA_DIAG_ESP32P4_CSI_ISP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct esp32p4_csi_isp_config_s
{
  uint32_t frame_width;
  uint32_t frame_height;
  uint32_t clk_hz;
  bool enable_fixed_wb;
};

int esp32p4_csi_isp_init(const struct esp32p4_csi_isp_config_s *cfg);
int esp32p4_csi_isp_deinit(void);

/* Read-only ISP control register (cntl) for runtime diagnostics.
 * Returns 0 if the ISP processor is not initialized. */
uint32_t esp32p4_csi_isp_get_cntl(void);

#endif /* APPS_EXAMPLES_CAMERA_DIAG_ESP32P4_CSI_ISP_H */
