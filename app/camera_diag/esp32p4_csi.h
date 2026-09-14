/****************************************************************************
 * apps/examples/camera_diag/esp32p4_csi.h
 *
 * S11-L1 camera build integration: minimal ESP32-P4 MIPI-CSI controller
 * (clock enable + mipi_csi_hal config + capture entry). Frame data path is
 * a build-level placeholder until a DMA/ISP data path is integrated under
 * separate hardware authorization.
 *
 * Controlled camera integration; not real label inspection.
 ****************************************************************************/

#ifndef APPS_EXAMPLES_CAMERA_DIAG_ESP32P4_CSI_H
#define APPS_EXAMPLES_CAMERA_DIAG_ESP32P4_CSI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct esp32p4_csi_config_s
{
  uint8_t  lanes_num;
  uint32_t frame_width;
  uint32_t frame_height;
  uint32_t in_bpp;
  uint32_t out_bpp;
  bool     byte_swap_en;
  int      lane_bit_rate_mbps;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int esp32p4_csi_init(FAR const struct esp32p4_csi_config_s *cfg);
int esp32p4_csi_brg_init(FAR const struct esp32p4_csi_config_s *cfg); /* L1.8.8 bridge/host only, no GDMA */
int esp32p4_csi_enable(void);
int esp32p4_csi_disable(void);
int esp32p4_csi_capture_one(FAR uint8_t *buffer, size_t buffer_length,
                            FAR uint32_t *timestamp_ms);
int esp32p4_csi_capture_one_rgb565(FAR uint8_t *buffer,
                                 size_t buffer_length,
                                 FAR uint32_t *timestamp_ms);
int esp32p4_csi_brg_set_rgb565_out(void);
int esp32p4_csi_install_block_isr(void); /* L1.8.25f R3-P2: DW-GDMA block-done ISR (ISP path) */
void esp32p4_csi_set_runtime_diag(bool enabled);
int esp32p4_csi_bridge_enable(void);
int esp32p4_csi_bridge_disable(void);
void esp32p4_csi_isp_dump(FAR const char *tag);
int esp32p4_csi_diag(void); /* L1.8.7 read-only CSI/GDMA register dump */
int esp32p4_csi_data_diag(void); /* L1.8.17 CSI data-plane dump (bridge FIFO/lane) */

#endif /* APPS_EXAMPLES_CAMERA_DIAG_ESP32P4_CSI_H */
