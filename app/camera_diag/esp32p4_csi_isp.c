/****************************************************************************
 * apps/examples/camera_diag/esp32p4_csi_isp.c
 *
 * L1.8.25e Stage B: NuttX camera ISP integration (build-level).
 * SC2336 RAW8 -> MIPI CSI (RAW8 bypass) -> ISP (RAW8->RGB565 demosaic).
 * References: official esp-video CSI+ISP path (verified on hardware) and
 * esp-hal-3rdparty upper_hal_isp (integrated in Stage A).
 *
 * Controlled camera diagnostic; not real label inspection.
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <stdio.h>
#include <debug.h>

#include "esp32p4_csi_isp.h"

#include "esp_private/mipi_csi_share_hw_ctrl.h"
#include "driver/isp_core.h"
#include "driver/isp_wbg.h"
#include "hal/isp_ll.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static isp_proc_handle_t g_isp_proc = NULL;
static int g_isp_brg_id = -1;
static bool g_isp_wbg_enabled;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int esp32p4_csi_isp_init(const struct esp32p4_csi_isp_config_s *cfg)
{
  esp_err_t ret;
  int csi_brg_id;

  if (cfg == NULL || cfg->frame_width == 0 || cfg->frame_height == 0)
    {
      return -EINVAL;
    }

  if (g_isp_proc != NULL)
    {
      return -EBUSY;
    }

  /* Claim the MIPI CSI bridge as a SHARE user: the ISP processor shares the
   * CSI bridge with the CSI controller (same as the verified official
   * esp-video path: SOC_ISP_SHARE_CSI_BRG). */
  ret = mipi_csi_brg_claim(MIPI_CSI_BRG_USER_SHARE, &csi_brg_id);
  if (ret != ESP_OK)
    {
      _err("csi_isp: mipi_csi_brg_claim failed: %d\n", ret);
      return -EIO;
    }

  /* Create the ISP processor: input RAW8 from CSI, output RGB565 (demosaic).
   * This mirrors the official example isp_config:
   *   input_data_source  = ISP_INPUT_DATA_SOURCE_CSI
   *   input_data_color_type  = ISP_COLOR_RAW8
   *   output_data_color_type = ISP_COLOR_RGB565 */
  esp_isp_processor_cfg_t isp_config = {
    .clk_hz = (cfg->clk_hz != 0) ? cfg->clk_hz : 80000000,
    .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
    .input_data_color_type = ISP_COLOR_RAW8,
    .output_data_color_type = ISP_COLOR_RGB565,
    .has_line_start_packet = false,
    .has_line_end_packet = false,
    .h_res = cfg->frame_width,
    .v_res = cfg->frame_height,
  };

  ret = esp_isp_new_processor(&isp_config, &g_isp_proc);
  if (ret != ESP_OK)
    {
      /* L1.8.25f R2: upper_hal_isp's err: label only frees the processor
       * context and does NOT declaim the bridge; this explicit declaim is
       * required to keep the ref-count balanced on this failure path. */
      _err("csi_isp: esp_isp_new_processor failed: %d\n", ret);
      mipi_csi_brg_declaim(csi_brg_id);
      g_isp_brg_id = -1;
      return -EIO;
    }

  if (cfg->enable_fixed_wb)
    {
      const esp_isp_wbg_config_t wbg_config =
      {
        .flags.update_once_configured = true,
      };
      const isp_wbg_gain_t wbg_gain =
      {
        .gain_r = 512,
        .gain_g = 256,
        .gain_b = 512,
      };

      ret = esp_isp_wbg_configure(g_isp_proc, &wbg_config);
      if (ret == ESP_OK)
        {
          ret = esp_isp_wbg_enable(g_isp_proc);
        }

      if (ret == ESP_OK)
        {
          g_isp_wbg_enabled = true;
          ret = esp_isp_wbg_set_wb_gain(g_isp_proc, wbg_gain);
        }

      if (ret != ESP_OK)
        {
          _err("csi_isp: fixed WBG setup failed: %d\n", ret);
          if (g_isp_wbg_enabled)
            {
              esp_isp_wbg_disable(g_isp_proc);
              g_isp_wbg_enabled = false;
            }

          esp_isp_del_processor(g_isp_proc);
          g_isp_proc = NULL;
          g_isp_brg_id = -1;
          return -EIO;
        }

      printf("csi_isp: fixed WBG enabled (R=512 G=256 B=512)\n");
    }

  ret = esp_isp_enable(g_isp_proc);
  if (ret != ESP_OK)
    {
      /* L1.8.25f R2: esp_isp_del_processor() below ALREADY declaims the
       * bridge; the extra explicit declaim is removed to avoid a double
      * declaim on this failure path. */
      _err("csi_isp: esp_isp_enable failed: %d\n", ret);
      if (g_isp_wbg_enabled)
        {
          esp_isp_wbg_disable(g_isp_proc);
          g_isp_wbg_enabled = false;
        }

      esp_isp_del_processor(g_isp_proc);
      g_isp_proc = NULL;
      g_isp_brg_id = -1;
      return -EIO;
    }

  g_isp_brg_id = csi_brg_id;
  _info("csi_isp: ISP processor enabled (RAW8->RGB565) brg=%d\n", csi_brg_id);
  return OK;
}

uint32_t esp32p4_csi_isp_get_cntl(void)
{
  if (g_isp_proc == NULL)
    {
      return 0;
    }

  /* Read-only ISP control register: mipi_data_en / isp_en / isp_in_src /
   * isp_out_type. Read via the SoC register block (no private type needed). */
  return ISP_LL_GET_HW(0)->cntl.val;
}

int esp32p4_csi_isp_deinit(void)
{
  if (g_isp_proc != NULL)
    {
      if (g_isp_wbg_enabled)
        {
          esp_isp_wbg_disable(g_isp_proc);
          g_isp_wbg_enabled = false;
        }

      esp_isp_disable(g_isp_proc);
      /* L1.8.25f R2: esp_isp_del_processor() ALREADY declaims the CSI
       * bridge (upper_hal_isp isp_core.c, SOC_ISP_SHARE_CSI_BRG branch:
       * mipi_csi_brg_declaim(proc->csi_brg_id)). Keeping the extra explicit
       * declaim below would double-declaim: with ref_cnt 2 (CSI+ISP) the
       * first declaim makes it 1, the second 0, and a repeated deinit could
       * underflow to -1 (ESP_ERR_INVALID_STATE). The explicit declaim is
       * therefore removed; the ref-count is balanced by:
       *   CSI claim (esp32p4_csi_brg_init)      -> +1 (2 total after ISP)
       *   disable declaim (esp32p4_csi_disable) -> -1
       *   del_processor declaim (here)          -> -1  -> 0  */
      esp_isp_del_processor(g_isp_proc);
      g_isp_proc = NULL;
    }

  g_isp_brg_id = -1;
  g_isp_wbg_enabled = false;

  _info("csi_isp: ISP deinitialized\n");
  return OK;
}
