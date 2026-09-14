/****************************************************************************
 * apps/examples/camera_diag/esp32p4_dsi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * L1.8.25f display-link Stage 1: EK79007 MIPI-DSI panel solid-color / color
 * bar diagnostic (NuttX). Mirrors the official ESP-IDF stack
 * (esp_lcd_mipi_dsi_bus.c + esp_lcd_panel_dpi.c + esp_lcd_ek79007.c) but
 * calls the esp-hal-3rdparty HAL/LL directly (no esp_lcd object layer):
 *
 *   1. MIPI PHY LDO (chan 3, 2.5 V) - same as the CSI path
 *   2. mipi_dsi_ll_enable_bus_clock + reset_register (bus 0)
 *   3. PHY PLL config clocks + mipi_dsi_hal_init + configure_phy_pll
 *   4. DSI host generic-interface config (CRC/ECC/EoTp/timeout/escape div)
 *   5. EK79007 init command table over the DBI (generic) channel
 *   6. DPI panel config: host DPI timing/color-coding + bridge config
 *      (RGB565 in/out, flow ctrl = DMA, burst 256, empty thresh 768)
 *   7. Solid color / color bar drawn into a PSRAM frame buffer
 *   8. DW-GDMA channel (MEM -> DSI bridge, flow ctrl SELF) keeps
 *      refreshing the panel; hold for a fixed window, then teardown.
 *
 * RESTRICTED diagnostic: no image capture/save/analysis, no network, no
 * camera involvement. Panel output is observed visually on the LCD only.
 *
 * Returned Value:
 *   OK (0) on success; negative errno on failure.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/arch.h>   /* up_udelay */
#include <nuttx/irq.h>

#include "irq.h"        /* DW_GDMA_INTR_SOURCE / ESP_SOURCE2IRQ */
#include "esp_irq.h"    /* esp_setup_irq / esp_teardown_irq */

#include "soc/soc.h"
#include "soc/reg_base.h"

#include "hal/mipi_dsi_hal.h"
#include "hal/mipi_dsi_ll.h"
#include "hal/mipi_dsi_host_ll.h"
#include "hal/mipi_dsi_brg_ll.h"
#include "hal/mipi_dsi_phy_ll.h"
#include "hal/mipi_dsi_periph.h"
#include "hal/dw_gdma_ll.h"

#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_reg.h"
#include "esp_private/esp_clk_tree_common.h"
#include "esp32p4_dsi.h"

/* DSI host default timeout/escape clock frequencies (official bus.c) */
#define DSI_DEFAULT_TIMEOUT_CLOCK_FREQ_MHZ  10
#define DSI_DEFAULT_ESCAPE_CLOCK_FREQ_MHZ   18

/* __DECLARE_RCC_ATOMIC_ENV shim: expands to the variable name callers
 * declare before using the clock-gate ll wrappers (build-level). */
#define __DECLARE_RCC_ATOMIC_ENV rcc_atomic_env

/* ------------------------------------------------------------------------
 * Panel / DSI configuration (official EK79007 defaults)
 * ---------------------------------------------------------------------- */

#define DSI_BUS_ID           0
#define DSI_NUM_LANES        2
#define DSI_LANE_MBPS        1000.0f

#define PANEL_H_RES          1024
#define PANEL_V_RES          600
#define PANEL_BPP            16                /* RGB565 */

#define DPI_CLK_SRC_MHZ      240.0f            /* P4 DPI default src = PLL_F240M (240 MHz) */
#define DPI_CLK_MHZ          48.0f

#define IMG_HSYNC            10
#define IMG_HBP              120
#define IMG_HFP              120
#define IMG_VSYNC            1
#define IMG_VBP              20
#define IMG_VFP              10

#define DSI_BRG_BURST_LEN    256
#define DSI_BRG_EMPTY_THRESH (1024 - 256)

#define DSI_GDMA_CHANNEL     1                 /* ch0 = CSI, ch1 = DSI */

#define DSI_PIN_LCD_RST      27
#define DSI_PIN_BK_LIGHT     26
#define DSI_TOUCH_RADIUS     55
#define DSI_TOUCH_DIAMETER   (2 * DSI_TOUCH_RADIUS + 1)

/* ------------------------------------------------------------------------
 * EK79007 init command table (official esp_lcd_ek79007 vendor_specific_init)
 * ---------------------------------------------------------------------- */

struct ek79007_init_cmd_s
{
  uint8_t cmd;
  const uint8_t *data;
  uint8_t data_bytes;
  uint8_t delay_ms;
};

#define EK79007_PAD_CONTROL  0xB2
#define EK79007_DSI_2_LANE   0x10

static const uint8_t d0 = 0x8B;
static const uint8_t d1 = 0x78;
static const uint8_t d2 = 0x84;
static const uint8_t d3 = 0x88;
static const uint8_t d4 = 0xA8;
static const uint8_t d5 = 0xE3;

static const struct ek79007_init_cmd_s s_ek79007_init_cmds[] =
{
  {0x80, &d0, 1, 0},
  {0x81, &d1, 1, 0},
  {0x82, &d2, 1, 0},
  {0x83, &d3, 1, 0},
  {0x84, &d4, 1, 0},
  {0x85, &d5, 1, 0},
  {0x86, &d3, 1, 0},          /* 0x86: 0x88 (official EK79007 default) */
  {0x11, NULL, 0, 120},       /* Sleep Out, 120 ms */
};

/* ------------------------------------------------------------------------
 * Static state
 * ---------------------------------------------------------------------- */

static mipi_dsi_hal_context_t s_dsi_hal;
static dw_gdma_dev_t *g_dsi_dma_dev;
static esp_ldo_channel_handle_t g_dsi_ldo;
static bool g_dsi_ldo_acquired;
static FAR uint8_t *g_dsi_status_ui_fb;
static bool g_dsi_status_ui_active;
static int g_dsi_inference_result_count = -1;
static int g_dsi_inference_energy_level = -1;
static bool g_dsi_label_detected;
static int g_dsi_workflow_stage;
static esp32p4_dsi_workflow_cb_t g_dsi_workflow_cb;
static bool g_dsi_workflow_blocked;
int esp32p4_dsi_status_ui_set_inspection_result(int candidate_count,
                                                int energy_level);
void esp32p4_dsi_status_ui_stop(void);
static bool g_dsi_touch_has_point;
static uint16_t g_dsi_touch_x;
static uint16_t g_dsi_touch_y;
static int g_dsi_touch_x0;
static int g_dsi_touch_y0;
static int g_dsi_touch_width;
static int g_dsi_touch_height;
static uint16_t g_dsi_touch_backup[DSI_TOUCH_DIAMETER * DSI_TOUCH_DIAMETER];

/* L1.8.25f display Stage 2 prep: the DW-GDMA controller is shared with the
 * CSI path (ch0 = CSI, ch1 = DSI). dw_gdma_ll_reset() is a CONTROLLER-level
 * soft reset (reset0.dmac_rst) that clears ALL channels, so we must not
 * re-run it once the controller is up. g_dsi_gdma_controller_init guards
 * the controller-level init; per-channel config below is safe to redo. */
static bool g_dsi_gdma_controller_init;

static void dsi_board_gpio_prepare(void)
{
  esp_rom_gpio_pad_select_gpio(DSI_PIN_LCD_RST);
  esp_rom_gpio_pad_select_gpio(DSI_PIN_BK_LIGHT);
  esp_rom_gpio_connect_out_signal(DSI_PIN_LCD_RST, 0x100, false, false);
  esp_rom_gpio_connect_out_signal(DSI_PIN_BK_LIGHT, 0x100, false, false);
  REG_SET_BIT(GPIO_ENABLE_W1TS_REG, (1u << DSI_PIN_LCD_RST) |
              (1u << DSI_PIN_BK_LIGHT));
  REG_SET_BIT(GPIO_OUT_W1TC_REG, (1u << DSI_PIN_BK_LIGHT));
}

static void dsi_board_panel_reset(void)
{
  REG_SET_BIT(GPIO_OUT_W1TC_REG, (1u << DSI_PIN_LCD_RST));
  up_mdelay(10);
  REG_SET_BIT(GPIO_OUT_W1TS_REG, (1u << DSI_PIN_LCD_RST));
  up_mdelay(20);
}

static void dsi_board_backlight_enable(void)
{
  REG_SET_BIT(GPIO_OUT_W1TS_REG, (1u << DSI_PIN_BK_LIGHT));
}

static void dsi_panel_io_config(void)
{
  dsi_host_dev_t *host = s_dsi_hal.host;

  mipi_dsi_host_ll_enable_te_ack(host, false);
  mipi_dsi_host_ll_enable_cmd_ack(host, true);
  mipi_dsi_host_ll_set_gen_short_wr_speed_mode(
      host, 0, MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_gen_short_wr_speed_mode(
      host, 1, MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_gen_short_wr_speed_mode(
      host, 2, MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_gen_long_wr_speed_mode(
      host, MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_gen_short_rd_speed_mode(
      host, 0, MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_gen_short_rd_speed_mode(
      host, 1, MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_gen_short_rd_speed_mode(
      host, 2, MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_dcs_short_wr_speed_mode(
      host, 0, MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_dcs_short_wr_speed_mode(
      host, 1, MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_dcs_long_wr_speed_mode(
      host, MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_dcs_short_rd_speed_mode(
      host, 0, MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_mrps_speed_mode(host, MIPI_DSI_LL_TRANS_SPEED_LP);
}

/* ------------------------------------------------------------------------
 * DSI bus init (mirrors esp_lcd_new_dsi_bus)
 * ---------------------------------------------------------------------- */

static int dsi_bus_init(void)
{
  mipi_dsi_hal_config_t hal_cfg;
  int rcc_atomic_env;

  /* APB clock + reset for DSI host/bridge registers */
  mipi_dsi_ll_enable_bus_clock(DSI_BUS_ID, true);
  mipi_dsi_ll_reset_register(DSI_BUS_ID);

  /* PHY PLL reference clock: P4 default = XTAL 40 MHz (MIPI_DSI_PHY_PLLREF
   * _CLK_SRC_DEFAULT = SOC_MOD_CLK_XTAL). Enable the source gate first
   * (official esp_lcd_new_dsi_bus does esp_clk_tree_enable_src). */
  esp_clk_tree_enable_src((soc_module_clk_t)MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT, true);
  mipi_dsi_ll_set_phy_pllref_clock_source(DSI_BUS_ID,
                                          MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT);
  mipi_dsi_ll_set_phy_pll_ref_clock_div(DSI_BUS_ID, 1);
  mipi_dsi_ll_enable_phy_pllref_clock(DSI_BUS_ID, true);

  /* PHY config clock (PLL_F20M) - enable source gate as well */
  esp_clk_tree_enable_src((soc_module_clk_t)MIPI_DSI_PHY_CFG_CLK_SRC_DEFAULT, true);
  mipi_dsi_ll_set_phy_config_clock_source(DSI_BUS_ID,
                                          MIPI_DSI_PHY_CFG_CLK_SRC_DEFAULT);
  mipi_dsi_ll_enable_phy_config_clock(DSI_BUS_ID, true);

  memset(&hal_cfg, 0, sizeof(hal_cfg));
  hal_cfg.bus_id           = DSI_BUS_ID;
  hal_cfg.lane_bit_rate_mbps = DSI_LANE_MBPS;
  hal_cfg.num_data_lanes   = DSI_NUM_LANES;

  mipi_dsi_hal_init(&s_dsi_hal, &hal_cfg);
  mipi_dsi_hal_configure_phy_pll(&s_dsi_hal, 40u * 1000u * 1000u,
                                 DSI_LANE_MBPS);

  /* Wait for PHY PLL lock and lanes stopped (official bus init) */
  {
    int lock_wait = 0;

    while (!mipi_dsi_phy_ll_is_pll_locked(s_dsi_hal.host))
      {
        up_udelay(1000);
        if (++lock_wait > 100)
          {
            printf("dsi: PHY PLL lock timeout\n");
            return -ETIMEDOUT;
          }
      }

    printf("dsi: PHY PLL locked\n");
  }

  {
    int lane_wait = 0;

    while (!mipi_dsi_phy_ll_are_lanes_stopped(s_dsi_hal.host, DSI_NUM_LANES))
      {
        up_udelay(1000);
        if (++lane_wait > 100)
          {
            printf("dsi: lane stop timeout\n");
            return -ETIMEDOUT;
          }
      }

    printf("dsi: lanes stopped\n");
  }

  /* Host generic-interface config */
  mipi_dsi_host_ll_enable_video_mode(s_dsi_hal.host, false);
  mipi_dsi_host_ll_set_clock_lane_state(s_dsi_hal.host,
                                        MIPI_DSI_LL_CLOCK_LANE_STATE_AUTO);
  mipi_dsi_phy_ll_set_switch_time(s_dsi_hal.host, 50, 104, 46, 128);
  mipi_dsi_host_ll_enable_rx_crc(s_dsi_hal.host, true);
  mipi_dsi_host_ll_enable_rx_ecc(s_dsi_hal.host, true);
  mipi_dsi_host_ll_enable_tx_eotp(s_dsi_hal.host, true, false);
  mipi_dsi_host_ll_set_timeout_clock_division(
      s_dsi_hal.host,
      (uint32_t)(DSI_LANE_MBPS / 8.0f /
                 DSI_DEFAULT_TIMEOUT_CLOCK_FREQ_MHZ + 0.5f));
  mipi_dsi_host_ll_set_escape_clock_division(
      s_dsi_hal.host,
      (uint32_t)(DSI_LANE_MBPS / 8.0f /
                 DSI_DEFAULT_ESCAPE_CLOCK_FREQ_MHZ + 0.5f));
  mipi_dsi_host_ll_set_timeout_count(s_dsi_hal.host, 0, 0, 0, 0, 0, 0, 0);
  mipi_dsi_phy_ll_set_max_read_time(s_dsi_hal.host, 6000);
  mipi_dsi_phy_ll_set_stop_wait_time(s_dsi_hal.host, 0x3f);

  return OK;
}

/* ------------------------------------------------------------------------
 * EK79007 panel init (mirrors panel_ek79007_send_init_cmds)
 * ---------------------------------------------------------------------- */

static void dsi_panel_send_init_cmds(void)
{
  int i;
  uint8_t lane_cmd = EK79007_DSI_2_LANE;

  /* Software reset (mirrors panel_ek79007_reset when no hardware RST pin):
   * 0x01 SWRESET + 20 ms, required before the vendor init sequence. */
  mipi_dsi_hal_host_gen_write_dcs_command(&s_dsi_hal, 0, 0x01, 1, NULL, 0);
  up_mdelay(20);

  /* PAD_CONTROL: 2-lane DSI */
  mipi_dsi_hal_host_gen_write_dcs_command(&s_dsi_hal, 0,
                                          EK79007_PAD_CONTROL, 1,
                                          &lane_cmd, 1);

  for (i = 0; i < (int)(sizeof(s_ek79007_init_cmds) /
                        sizeof(s_ek79007_init_cmds[0])); i++)
    {
      if (s_ek79007_init_cmds[i].data_bytes > 0)
        {
          mipi_dsi_hal_host_gen_write_dcs_command(
              &s_dsi_hal, 0,
              s_ek79007_init_cmds[i].cmd, 1,
              s_ek79007_init_cmds[i].data,
              s_ek79007_init_cmds[i].data_bytes);
        }
      else
        {
          mipi_dsi_hal_host_gen_write_dcs_command(
              &s_dsi_hal, 0,
              s_ek79007_init_cmds[i].cmd, 1,
              NULL, 0);
        }

      if (s_ek79007_init_cmds[i].delay_ms > 0)
        {
          up_mdelay(s_ek79007_init_cmds[i].delay_ms);
        }
    }

  printf("dsi: ek79007 init cmds sent (%d cmds)\n",
         (int)(sizeof(s_ek79007_init_cmds) / sizeof(s_ek79007_init_cmds[0])));
}

/* ------------------------------------------------------------------------
 * DPI panel config (mirrors esp_lcd_new_panel_dpi + dpi_panel_init)
 * ---------------------------------------------------------------------- */

static void dsi_dpi_config(void)
{
  uint32_t dpi_div;
  uint32_t num_pixel_bits;
  uint32_t fb_bytes;
  int rcc_atomic_env;

  fb_bytes = PANEL_H_RES * PANEL_V_RES * PANEL_BPP / 8;

  dsi_board_gpio_prepare();
  num_pixel_bits = PANEL_H_RES * PANEL_V_RES * PANEL_BPP;

  /* DPI clock: default source PLL_F240M (240 MHz) -> divide to 48 MHz.
   * The PLL output gate must be enabled first (official esp_lcd_new_panel_dpi
   * calls esp_clk_tree_enable_src(dpi_clk_src, true)); without it the DPI
   * clock does not run and the panel stays black. */
  esp_clk_tree_enable_src((soc_module_clk_t)MIPI_DSI_DPI_CLK_SRC_DEFAULT, true);
  mipi_dsi_ll_set_dpi_clock_source(DSI_BUS_ID, MIPI_DSI_DPI_CLK_SRC_DEFAULT);
  dpi_div = mipi_dsi_hal_host_dpi_calculate_divider(&s_dsi_hal,
                                                    DPI_CLK_SRC_MHZ,
                                                    DPI_CLK_MHZ);
  mipi_dsi_ll_set_dpi_clock_div(DSI_BUS_ID, dpi_div);
  mipi_dsi_ll_enable_dpi_clock(DSI_BUS_ID, true);

  /* Host DPI: vcid, color coding, timing polarity, LP transitions */
  mipi_dsi_host_ll_dpi_set_vcid(s_dsi_hal.host, 0);
  mipi_dsi_host_ll_dpi_set_color_coding(s_dsi_hal.host,
                                        LCD_COLOR_FMT_RGB565, 0);
  mipi_dsi_host_ll_dpi_set_timing_polarity(s_dsi_hal.host,
                                           false, false, false, false, false);

  /* allow LP in all video periods and for commands */
  mipi_dsi_host_ll_dpi_enable_lp_horizontal_timing(s_dsi_hal.host,
                                                   true, true);
  mipi_dsi_host_ll_dpi_enable_lp_vertical_timing(s_dsi_hal.host,
                                                 true, true, true, true);
  mipi_dsi_host_ll_dpi_enable_lp_command(s_dsi_hal.host, true);

  mipi_dsi_host_ll_dpi_enable_frame_ack(s_dsi_hal.host, true);
  mipi_dsi_host_ll_dpi_set_video_burst_type(
      s_dsi_hal.host, MIPI_DSI_LL_VIDEO_BURST_WITH_SYNC_PULSES);
  mipi_dsi_host_ll_dpi_set_video_packet_pixel_num(s_dsi_hal.host,
                                                  PANEL_H_RES);
  mipi_dsi_host_ll_dpi_set_trunks_num(s_dsi_hal.host, 0);
  mipi_dsi_host_ll_dpi_set_null_packet_size(s_dsi_hal.host, 0);

  mipi_dsi_hal_host_dpi_set_horizontal_timing(&s_dsi_hal,
                                              IMG_HSYNC, IMG_HBP,
                                              PANEL_H_RES, IMG_HFP);
  mipi_dsi_hal_host_dpi_set_vertical_timing(&s_dsi_hal,
                                            IMG_VSYNC, IMG_VBP,
                                            PANEL_V_RES, IMG_VFP);

  /* Bridge config */
  mipi_dsi_brg_ll_set_num_pixel_bits(s_dsi_hal.bridge, num_pixel_bits);
  mipi_dsi_brg_ll_set_underrun_discard_count(s_dsi_hal.bridge, PANEL_H_RES);
  mipi_dsi_brg_ll_set_input_color_format(s_dsi_hal.bridge,
                                         LCD_COLOR_FMT_RGB565);
  mipi_dsi_brg_ll_set_output_color_format(s_dsi_hal.bridge,
                                          LCD_COLOR_FMT_RGB565, 0);
  mipi_dsi_brg_ll_set_flow_controller(s_dsi_hal.bridge,
                                      MIPI_DSI_LL_FLOW_CONTROLLER_DMA);
  mipi_dsi_brg_ll_set_multi_block_number(s_dsi_hal.bridge, 1);
  mipi_dsi_brg_ll_set_burst_len(s_dsi_hal.bridge, DSI_BRG_BURST_LEN);
  mipi_dsi_brg_ll_set_empty_threshold(s_dsi_hal.bridge,
                                      DSI_BRG_EMPTY_THRESH);
  mipi_dsi_brg_ll_enable(s_dsi_hal.bridge, true);
  mipi_dsi_brg_ll_update_dpi_config(s_dsi_hal.bridge);

  printf("dsi: dpi config done (dpi_div=%lu fb_bytes=%lu)\n",
         (unsigned long)dpi_div, (unsigned long)fb_bytes);
}

/* Read-only snapshot for panels which accept the command sequence and
 * backlight but remain black.  The active registers show whether the host
 * latched the DPI timing; PHY status and host status distinguish a link-side
 * failure from a panel-side timing or format mismatch. */
static void dsi_host_phy_dump(FAR const char *tag)
{
  dsi_host_dev_t *host = s_dsi_hal.host;

  printf("dsi: host@%s pwr=0x%08lx mode=0x%08lx vid=0x%08lx pkt=0x%08lx "
         "hsa=0x%08lx hbp=0x%08lx hline=0x%08lx vsa=0x%08lx "
         "vbp=0x%08lx vfp=0x%08lx vact=0x%08lx\n",
         tag, (unsigned long)host->pwr_up.val,
         (unsigned long)host->mode_cfg.val,
         (unsigned long)host->vid_mode_cfg.val,
         (unsigned long)host->vid_pkt_size.val,
         (unsigned long)host->vid_hsa_time.val,
         (unsigned long)host->vid_hbp_time.val,
         (unsigned long)host->vid_hline_time.val,
         (unsigned long)host->vid_vsa_lines.val,
         (unsigned long)host->vid_vbp_lines.val,
         (unsigned long)host->vid_vfp_lines.val,
         (unsigned long)host->vid_vactive_lines.val);

  printf("dsi: host@%s active_vid=0x%08lx active_pkt=0x%08lx "
         "active_hline=0x%08lx active_vact=0x%08lx phy_rstz=0x%08lx "
         "phy_if=0x%08lx phy_status=0x%08lx lpclk=0x%08lx\n",
         tag, (unsigned long)host->vid_mode_cfg_act.val,
         (unsigned long)host->vid_pkt_size_act.val,
         (unsigned long)host->vid_hline_time_act.val,
         (unsigned long)host->vid_vactive_lines_act.val,
         (unsigned long)host->phy_rstz.val,
         (unsigned long)host->phy_if_cfg.val,
         (unsigned long)host->phy_status.val,
         (unsigned long)host->lpclk_ctrl.val);

  printf("dsi: host@%s int0=0x%08lx int1=0x%08lx mask0=0x%08lx "
         "mask1=0x%08lx\n",
         tag, (unsigned long)host->int_st0.val,
         (unsigned long)host->int_st1.val,
         (unsigned long)host->int_msk0.val,
         (unsigned long)host->int_msk1.val);
}

/* ------------------------------------------------------------------------
 * DW-GDMA channel for MEM -> DSI bridge (flow ctrl SELF).
 *
 * The official DPI driver uses one valid, last LLI and re-arms it from its
 * DMA-complete ISR. A self-referential LLI is not valid on this DW-GDMA and
 * raises SHADOWREG_OR_LLI_INVALID_ERR after the first frame.
 * ---------------------------------------------------------------------- */

static dw_gdma_link_list_item_t *g_dsi_lli;
static volatile bool g_dsi_gdma_armed;
static bool g_dsi_gdma_isr_attached;
static int g_dsi_gdma_cpuint = -1;
static bool g_dsi_gdma_shared_irq;

static void dsi_gdma_dump_lli(void)
{
  FAR volatile uint32_t *lli_nc;
  intptr_t current;

  if (g_dsi_dma_dev == NULL || g_dsi_lli == NULL)
    {
      return;
    }

  lli_nc = (FAR volatile uint32_t *)((uintptr_t)g_dsi_lli +
                                     SOC_NON_CACHEABLE_OFFSET_SRAM);
  current = dw_gdma_ll_channel_get_current_link_list_item_addr(
    g_dsi_dma_dev, DSI_GDMA_CHANNEL);

  printf("dsi: lli head=%p current=0x%08lx w0=%08lx w1=%08lx w2=%08lx "
         "w3=%08lx w4=%08lx w5=%08lx w6=%08lx w7=%08lx\n",
         g_dsi_lli, (unsigned long)current,
         (unsigned long)lli_nc[0], (unsigned long)lli_nc[1],
         (unsigned long)lli_nc[2], (unsigned long)lli_nc[3],
         (unsigned long)lli_nc[4], (unsigned long)lli_nc[5],
         (unsigned long)lli_nc[6], (unsigned long)lli_nc[7]);
  printf("dsi: lli w8=%08lx w9=%08lx w10=%08lx w11=%08lx w12=%08lx "
         "w13=%08lx w14=%08lx w15=%08lx\n",
         (unsigned long)lli_nc[8], (unsigned long)lli_nc[9],
         (unsigned long)lli_nc[10], (unsigned long)lli_nc[11],
         (unsigned long)lli_nc[12], (unsigned long)lli_nc[13],
         (unsigned long)lli_nc[14], (unsigned long)lli_nc[15]);
}

void esp32p4_dsi_gdma_irq_handler(void)
{
  dw_gdma_dev_t *dev = g_dsi_dma_dev;
  uint32_t st;

  if (dev == NULL)
    {
      return;
    }

  st = dw_gdma_ll_channel_get_intr_status(dev, DSI_GDMA_CHANNEL);
  dw_gdma_ll_channel_clear_intr(dev, DSI_GDMA_CHANNEL, st);

  /* Debug: track ISR calls */
  static int isr_count = 0;
  isr_count++;
  if (isr_count <= 5 || isr_count % 100 == 0)
    {
      printf("dsi: ISR called #%d st=0x%08lx armed=%d lli=%p\n",
             isr_count, (unsigned long)st, (int)g_dsi_gdma_armed, g_dsi_lli);
    }

  if ((st & DW_GDMA_LL_CHANNEL_EVENT_DMA_TFR_DONE) != 0 &&
      g_dsi_gdma_armed && g_dsi_lli != NULL)
    {
      FAR dw_gdma_link_list_item_t *lli_nc =
        (FAR dw_gdma_link_list_item_t *)((uintptr_t)g_dsi_lli +
                                         SOC_NON_CACHEABLE_OFFSET_SRAM);

      /* The terminal item is consumed by hardware. Revalidate it before
       * reloading, exactly as esp_lcd_panel_dpi does in its completion ISR. */
      dw_gdma_ll_lli_set_block_markers(lli_nc, false, true, true);
      dw_gdma_ll_channel_set_link_list_head_addr(
        dev, DSI_GDMA_CHANNEL, (uint32_t)(uintptr_t)g_dsi_lli);
      dw_gdma_ll_channel_set_link_list_master_port(
        dev, DSI_GDMA_CHANNEL, DW_GDMA_LL_MASTER_PORT_MEMORY);
      dw_gdma_ll_channel_enable(dev, DSI_GDMA_CHANNEL, true);

      if (isr_count <= 5)
        {
          printf("dsi: ISR re-armed DMA\n");
        }
    }

}

static int dsi_gdma_done_isr(int irq, FAR void *arg, FAR void *context)
{
  esp32p4_dsi_gdma_irq_handler();
  return 0;
}

static int dsi_gdma_install_done_isr(void)
{
  dw_gdma_dev_t *dev = g_dsi_dma_dev;
  int ret;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  if (g_dsi_gdma_isr_attached)
    {
      return OK;
    }

  /* Keep non-completion events out of the CLIC and clear any residue before
   * routing the DMA source. This mirrors the proven CSI R4-D2 lifecycle. */
  dw_gdma_ll_channel_enable_intr_propagation(dev, DSI_GDMA_CHANNEL,
                                             0xffffffffu, false);
  dw_gdma_ll_channel_clear_intr(dev, DSI_GDMA_CHANNEL, 0xffffffffu);
  dw_gdma_ll_channel_enable_intr_generation(
    dev, DSI_GDMA_CHANNEL, DW_GDMA_LL_CHANNEL_EVENT_DMA_TFR_DONE, true);
  dw_gdma_ll_channel_enable_intr_propagation(
    dev, DSI_GDMA_CHANNEL, DW_GDMA_LL_CHANNEL_EVENT_DMA_TFR_DONE, true);

  ret = esp_setup_irq(DW_GDMA_INTR_SOURCE, ESP_IRQ_PRIORITY_DEFAULT,
                      ESP_IRQ_TRIGGER_LEVEL, dsi_gdma_done_isr, NULL);
  if (ret < 0)
    {
      return -EIO;
    }

  g_dsi_gdma_cpuint = ret;
  up_enable_irq(ESP_SOURCE2IRQ(DW_GDMA_INTR_SOURCE));
  g_dsi_gdma_isr_attached = true;
  return OK;
}

static void dsi_gdma_uninstall_done_isr(void)
{
  dw_gdma_dev_t *dev = g_dsi_dma_dev;

  g_dsi_gdma_armed = false;

  if (dev != NULL)
    {
      dw_gdma_ll_channel_enable_intr_propagation(dev, DSI_GDMA_CHANNEL,
                                                 0xffffffffu, false);
      dw_gdma_ll_channel_clear_intr(dev, DSI_GDMA_CHANNEL, 0xffffffffu);
    }

  /* The live preview shares the source route installed by CSI.  It owns
   * channel 1's mask/status only; never tear down CSI's cpuint mapping. */
  if (g_dsi_gdma_isr_attached && !g_dsi_gdma_shared_irq)
    {
      esp_teardown_irq(DW_GDMA_INTR_SOURCE, g_dsi_gdma_cpuint);
      g_dsi_gdma_isr_attached = false;
      g_dsi_gdma_cpuint = -1;
    }
}

static int dsi_gdma_start(FAR uint8_t *fb, uint32_t fb_bytes)
{
  dw_gdma_dev_t *dev = g_dsi_dma_dev;
  uint8_t ch = DSI_GDMA_CHANNEL;
  uint32_t items = fb_bytes / 8;   /* 64-bit transfer items */
  dw_gdma_link_list_item_t *lli;
  dw_gdma_link_list_item_t *lli_nc;   /* non-cacheable alias for LLI writes */
  int rcc_atomic_env;
  int ret;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  /* Reset + enable controller ONCE (shared with CSI; a controller-level
   * reset would wipe the CSI channel too). Stage 2 flows call this after
   * the CSI path has already brought the DMAC up, so we only initialize
   * the controller here when it has not been done before. */
  if (!g_dsi_gdma_controller_init)
    {
      dw_gdma_ll_enable_bus_clock(0, true);
      dw_gdma_ll_reset_register(0);
      dw_gdma_ll_reset(dev);
      dw_gdma_ll_enable_controller(dev, true);
      dw_gdma_ll_enable_intr_global(dev, true);
      g_dsi_gdma_controller_init = true;
    }

  /* Channel: MEM(src, increment) -> DSI bridge(dst, fixed), LIST mode */
  dw_gdma_ll_channel_set_trans_flow(dev, ch,
                                    DW_GDMA_ROLE_MEM,
                                    DW_GDMA_ROLE_PERIPH_DSI,
                                    DW_GDMA_FLOW_CTRL_SELF);
  dw_gdma_ll_channel_set_src_handshake_interface(dev, ch,
                                                 DW_GDMA_HANDSHAKE_HW);
  dw_gdma_ll_channel_set_dst_handshake_interface(dev, ch,
                                                 DW_GDMA_HANDSHAKE_HW);
  dw_gdma_ll_channel_set_dst_handshake_periph(dev, ch,
                                              DW_GDMA_ROLE_PERIPH_DSI);

  dw_gdma_ll_channel_set_src_burst_mode(dev, ch, DW_GDMA_BURST_MODE_INCREMENT);
  dw_gdma_ll_channel_set_src_burst_items(dev, ch, DW_GDMA_BURST_ITEMS_512);
  dw_gdma_ll_channel_set_src_burst_len(dev, ch, 16);
  dw_gdma_ll_channel_set_src_trans_width(dev, ch, DW_GDMA_TRANS_WIDTH_64);

  dw_gdma_ll_channel_set_dst_burst_mode(dev, ch, DW_GDMA_BURST_MODE_FIXED);
  dw_gdma_ll_channel_set_dst_burst_items(dev, ch, DW_GDMA_BURST_ITEMS_256);
  dw_gdma_ll_channel_set_dst_burst_len(dev, ch, 16);
  dw_gdma_ll_channel_set_dst_trans_width(dev, ch, DW_GDMA_TRANS_WIDTH_64);

  /* Linked-list based multi-block (one terminal item). */
  dw_gdma_ll_channel_set_src_multi_block_type(dev, ch,
                                              DW_GDMA_BLOCK_TRANSFER_LIST);
  dw_gdma_ll_channel_set_dst_multi_block_type(dev, ch,
                                              DW_GDMA_BLOCK_TRANSFER_LIST);
  dw_gdma_ll_channel_set_src_outstanding_limit(dev, ch, 5);
  dw_gdma_ll_channel_set_dst_outstanding_limit(dev, ch, 2);
  dw_gdma_ll_channel_set_priority(dev, ch, 1);

  /* Single terminal LLI item: fb -> DSI bridge. */
  lli = (dw_gdma_link_list_item_t *)
    heap_caps_aligned_alloc(DW_GDMA_LL_LINK_LIST_ALIGNMENT,
                            sizeof(dw_gdma_link_list_item_t),
                            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (lli == NULL)
    {
      printf("dsi: lli alloc failed\n");
      return -ENOMEM;
    }

  /* R4-D3: write the LLI through its non-cacheable alias, after a one-shot
   * cache sync, exactly like esp-idf dw_gdma_new_link_list. Writing via the
   * cached alias without msync left the fields in L2 cache while the DMA
   * engine read stale memory -> SHADOWREG_OR_LLI_INVALID_ERR (gdma_intr
   * bit13) and gdma_tr stayed 0. The descriptor and head use cached
   * addresses, as required by the DMA link-list engine. */
  esp_cache_msync(lli, sizeof(*lli),
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE |
                  ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  lli_nc = (dw_gdma_link_list_item_t *)
    ((uintptr_t)lli + SOC_NON_CACHEABLE_OFFSET_SRAM);
  memset(lli_nc, 0, sizeof(*lli));
  dw_gdma_ll_lli_set_src_addr(lli_nc, (uint32_t)(uintptr_t)fb);
  dw_gdma_ll_lli_set_dst_addr(lli_nc, MIPI_DSI_BRG_MEM_BASE);
  dw_gdma_ll_lli_set_trans_block_size(lli_nc, items);
  dw_gdma_ll_lli_set_src_master_port(lli_nc, (intptr_t)fb);
  dw_gdma_ll_lli_set_dst_master_port(lli_nc, MIPI_DSI_BRG_MEM_BASE);
  dw_gdma_ll_lli_set_src_trans_width(lli_nc, DW_GDMA_TRANS_WIDTH_64);
  dw_gdma_ll_lli_set_dst_trans_width(lli_nc, DW_GDMA_TRANS_WIDTH_64);
  dw_gdma_ll_lli_set_src_burst_mode(lli_nc, DW_GDMA_BURST_MODE_INCREMENT);
  dw_gdma_ll_lli_set_dst_burst_mode(lli_nc, DW_GDMA_BURST_MODE_FIXED);
  dw_gdma_ll_lli_set_src_burst_items(lli_nc, DW_GDMA_BURST_ITEMS_512);
  dw_gdma_ll_lli_set_dst_burst_items(lli_nc, DW_GDMA_BURST_ITEMS_256);
  dw_gdma_ll_lli_set_src_burst_len(lli_nc, 16);
  dw_gdma_ll_lli_set_dst_burst_len(lli_nc, 16);
  dw_gdma_ll_lli_set_link_list_master_port(lli_nc,
                                           DW_GDMA_LL_MASTER_PORT_MEMORY);
  dw_gdma_ll_lli_set_next_item_addr(lli_nc, 0);
  dw_gdma_ll_lli_set_block_markers(lli_nc, false, true, true);

  printf("dsi: gdma_start shared_irq=%d isr_attached=%d\n",
         (int)g_dsi_gdma_shared_irq, (int)g_dsi_gdma_isr_attached);

  if (g_dsi_gdma_shared_irq)
    {
      /* CSI owns DW_GDMA_INTR_SOURCE in the live session.  Configure only
       * channel 1's completion event; do not replace CSI's demux handler. */
      dw_gdma_ll_channel_enable_intr_propagation(dev, ch, 0xffffffffu, false);
      dw_gdma_ll_channel_clear_intr(dev, ch, 0xffffffffu);
      dw_gdma_ll_channel_enable_intr_generation(
        dev, ch, DW_GDMA_LL_CHANNEL_EVENT_DMA_TFR_DONE, true);
      dw_gdma_ll_channel_enable_intr_propagation(
        dev, ch, DW_GDMA_LL_CHANNEL_EVENT_DMA_TFR_DONE, true);
    }
  else
    {
      ret = dsi_gdma_install_done_isr();
      if (ret < 0)
        {
          heap_caps_free(lli);
          return ret;
        }
    }

  dw_gdma_ll_channel_set_link_list_head_addr(dev, ch, (uint32_t)(uintptr_t)lli);
  dw_gdma_ll_channel_set_link_list_master_port(dev, ch, DW_GDMA_LL_MASTER_PORT_MEMORY);
  g_dsi_lli = lli;
  g_dsi_gdma_armed = true;

  dw_gdma_ll_channel_enable(dev, ch, true);

  printf("dsi: gdma ch%u one-shot-lli started (fb=%p items=%lu lli=%p) shared_irq=%d\n",
         (unsigned int)ch, fb, (unsigned long)items, lli,
         (int)g_dsi_gdma_shared_irq);
  return OK;
}

static void dsi_gdma_stop(void)
{
  dw_gdma_dev_t *dev = g_dsi_dma_dev;

  dsi_gdma_uninstall_done_isr();

  if (dev != NULL)
    {
      dw_gdma_ll_channel_enable(dev, DSI_GDMA_CHANNEL, false);
      dw_gdma_ll_channel_abort(dev, DSI_GDMA_CHANNEL);
      dw_gdma_ll_channel_clear_intr(dev, DSI_GDMA_CHANNEL, 0xffffffffu);
    }

  if (g_dsi_lli != NULL)
    {
      heap_caps_free(g_dsi_lli);
      g_dsi_lli = NULL;
    }
}

/* ------------------------------------------------------------------------
 * Pattern drawing (solid color / vertical color bar), RGB565 PSRAM fb
 * ---------------------------------------------------------------------- */

#define RGB565_RED   0xF800
#define RGB565_GREEN 0x07E0
#define RGB565_BLUE  0x001F
#define RGB565_WHITE 0xFFFF
#define RGB565_BLACK 0x0000
#define RGB565_YELLOW 0xFFE0
#define RGB565_CYAN   0x07FF
#define RGB565_MAGENTA 0xF81F
#define RGB565_DARK_BLUE 0x0011
#define RGB565_PANEL_BLUE 0x021F
#define RGB565_TEAL 0x067B
#define RGB565_DARK_GREEN 0x04A0
#define RGB565_BUTTON_NORMAL 0x2124
#define RGB565_BUTTON_DISABLED 0x3186

/* 5x7 ASCII font.  The status UI deliberately stays self-contained instead
 * of adding a GUI framework to the 4 MB target image. */
static const uint8_t g_dsi_font[37][5] =
{
  {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
  {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c},
  {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
  {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
  {0x00, 0x41, 0x7f, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3f, 0x01},
  {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
  {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
  {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06},
  {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
  {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
  {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f},
  {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63},
  {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
  {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00},
  {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31},
  {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
  {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
  {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e},
  {0x00, 0x00, 0x00, 0x00, 0x00},
};

static const uint8_t *dsi_font_glyph(char ch)
{
  if (ch >= 'A' && ch <= 'Z')
    {
      return g_dsi_font[ch - 'A'];
    }

  if (ch >= '0' && ch <= '9')
    {
      return g_dsi_font[26 + ch - '0'];
    }

  return g_dsi_font[36];
}

static void dsi_fill_rect(uint16_t *px, int x, int y, int width, int height,
                          uint16_t color)
{
  int row;
  int col;

  for (row = y; row < y + height && row < PANEL_V_RES; row++)
    {
      if (row < 0)
        {
          continue;
        }

      for (col = x; col < x + width && col < PANEL_H_RES; col++)
        {
          if (col >= 0)
            {
              px[row * PANEL_H_RES + col] = color;
            }
        }
    }
}

static void dsi_draw_text(uint16_t *px, int x, int y, int scale,
                          const char *text, uint16_t color)
{
  int col;
  int row;
  int sx;
  int sy;

  while (*text != '\0')
    {
      const uint8_t *glyph = dsi_font_glyph(*text++);

      for (col = 0; col < 5; col++)
        {
          for (row = 0; row < 7; row++)
            {
              if ((glyph[col] & (1u << row)) == 0)
                {
                  continue;
                }

              for (sy = 0; sy < scale; sy++)
                {
                  for (sx = 0; sx < scale; sx++)
                    {
                      int draw_x = x + col * scale + sx;
                      int draw_y = y + row * scale + sy;

                      if (draw_x >= 0 && draw_x < PANEL_H_RES &&
                          draw_y >= 0 && draw_y < PANEL_V_RES)
                        {
                          px[draw_y * PANEL_H_RES + draw_x] = color;
                        }
                    }
                }
            }
        }

      x += 6 * scale;
    }
}

static void dsi_draw_line(uint16_t *px, int x0, int y0, int x1, int y1,
                          uint16_t color)
{
  int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
  int sx = x0 < x1 ? 1 : -1;
  int dy = y1 >= y0 ? y0 - y1 : y1 - y0;
  int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  for (;;)
    {
      dsi_fill_rect(px, x0 - 1, y0 - 1, 3, 3, color);
      if (x0 == x1 && y0 == y1)
        {
          break;
        }

      if (2 * error >= dy)
        {
          error += dy;
          x0 += sx;
        }

      if (2 * error <= dx)
        {
          error += dx;
          y0 += sy;
        }
    }
}

static void dsi_draw_touch_marker(uint16_t *px, int x, int y)
{
  dsi_fill_rect(px, x - 12, y - 1, 25, 3, RGB565_YELLOW);
  dsi_fill_rect(px, x - 1, y - 12, 3, 25, RGB565_YELLOW);
  dsi_fill_rect(px, x - 4, y - 4, 9, 9, RGB565_RED);
}

static void dsi_draw_touch_info(uint16_t *px, uint16_t x, uint16_t y,
                                unsigned int event_count)
{
  char text[28];

  snprintf(text, sizeof(text), "N%04u X%04u Y%03u", event_count % 10000,
           (unsigned int)x, (unsigned int)y);
  dsi_fill_rect(px, 740, 18, 250, 60, RGB565_TEAL);
  dsi_draw_text(px, 748, 28, 2, text, RGB565_BLACK);
}

static uint16_t dsi_darken_rgb565(uint16_t color)
{
  uint16_t red = (color >> 11) & 0x1f;
  uint16_t green = (color >> 5) & 0x3f;
  uint16_t blue = color & 0x1f;

  /* Keep the original hue while making the pressed area unmistakably deeper. */
  red /= 2;
  green /= 2;
  blue /= 2;
  return (red << 11) | (green << 5) | blue;
}

static void dsi_draw_touch_press(uint16_t *px, int x, int y)
{
  int dx;
  int dy;

  for (dy = -DSI_TOUCH_RADIUS; dy <= DSI_TOUCH_RADIUS; dy++)
    {
      int draw_y = y + dy;

      if (draw_y < 0 || draw_y >= PANEL_V_RES)
        {
          continue;
        }

      for (dx = -DSI_TOUCH_RADIUS; dx <= DSI_TOUCH_RADIUS; dx++)
        {
          int draw_x = x + dx;

          if (draw_x < 0 || draw_x >= PANEL_H_RES ||
              dx * dx + dy * dy > DSI_TOUCH_RADIUS * DSI_TOUCH_RADIUS)
            {
              continue;
            }

          px[draw_y * PANEL_H_RES + draw_x] =
            dsi_darken_rgb565(px[draw_y * PANEL_H_RES + draw_x]);
        }
    }
}

static void dsi_cache_sync_rect(FAR uint8_t *fb, int x0, int y0,
                                int x1, int y1)
{
  uintptr_t start;
  uintptr_t end;

  if (x0 < 0)
    {
      x0 = 0;
    }
  if (y0 < 0)
    {
      y0 = 0;
    }
  if (x1 >= PANEL_H_RES)
    {
      x1 = PANEL_H_RES - 1;
    }
  if (y1 >= PANEL_V_RES)
    {
      y1 = PANEL_V_RES - 1;
    }
  if (x0 > x1 || y0 > y1)
    {
      return;
    }

  start = (uintptr_t)fb + 2 * (y0 * PANEL_H_RES + x0);
  end = (uintptr_t)fb + 2 * ((y1 + 1) * PANEL_H_RES);
  start &= ~((uintptr_t)63);
  end = (end + 63) & ~((uintptr_t)63);
  esp_cache_msync((FAR void *)start, end - start,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

/* Continuously refresh: DMA ISR auto-re-arms on completion.  After modifying
 * pixels in task context, just cache-sync and the next ISR cycle picks them up.
 * Only (re)start DMA when it is not already running. */
static void dsi_submit_status_ui_frame(void)
{
  if (!g_dsi_status_ui_active || g_dsi_status_ui_fb == NULL)
    {
      return;
    }

  /* Cache sync the entire framebuffer so DMA sees the latest pixels. */
  esp_cache_msync(g_dsi_status_ui_fb,
                  PANEL_H_RES * PANEL_V_RES * sizeof(uint16_t),
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  /* If DMA is already running (ISR will auto re-arm), just return.
   * The next DMA completion interrupt will reload the latest content. */
  if (g_dsi_gdma_armed)
    {
      return;
    }

  /* If DMA was stopped (first start or recovery), restart it. */
  dsi_gdma_start(g_dsi_status_ui_fb,
                 PANEL_H_RES * PANEL_V_RES * sizeof(uint16_t));
}

static void dsi_touch_backup_region(uint16_t *px, int x, int y)
{
  int row;

  g_dsi_touch_x0 = x - DSI_TOUCH_RADIUS;
  g_dsi_touch_y0 = y - DSI_TOUCH_RADIUS;
  if (g_dsi_touch_x0 < 0)
    {
      g_dsi_touch_x0 = 0;
    }
  if (g_dsi_touch_y0 < 0)
    {
      g_dsi_touch_y0 = 0;
    }

  g_dsi_touch_width = DSI_TOUCH_DIAMETER;
  g_dsi_touch_height = DSI_TOUCH_DIAMETER;
  if (g_dsi_touch_x0 + g_dsi_touch_width > PANEL_H_RES)
    {
      g_dsi_touch_width = PANEL_H_RES - g_dsi_touch_x0;
    }
  if (g_dsi_touch_y0 + g_dsi_touch_height > PANEL_V_RES)
    {
      g_dsi_touch_height = PANEL_V_RES - g_dsi_touch_y0;
    }

  for (row = 0; row < g_dsi_touch_height; row++)
    {
      memcpy(&g_dsi_touch_backup[row * DSI_TOUCH_DIAMETER],
             &px[(g_dsi_touch_y0 + row) * PANEL_H_RES + g_dsi_touch_x0],
             g_dsi_touch_width * sizeof(uint16_t));
    }
}

static void dsi_touch_restore_region(uint16_t *px)
{
  int row;

  for (row = 0; row < g_dsi_touch_height; row++)
    {
      memcpy(&px[(g_dsi_touch_y0 + row) * PANEL_H_RES + g_dsi_touch_x0],
             &g_dsi_touch_backup[row * DSI_TOUCH_DIAMETER],
             g_dsi_touch_width * sizeof(uint16_t));
    }
}

static const char *dsi_workflow_stage_name(int stage);

static void dsi_draw_inference_result(uint16_t *px, int defect_count,
                                      int energy_level)
{
  char count_text[20];
  char energy_text[24];

  dsi_fill_rect(px, 48, 278, PANEL_H_RES - 96, 182, RGB565_PANEL_BLUE);
  if (g_dsi_workflow_blocked)
    {
      dsi_draw_text(px, 84, 306, 5, "LABEL REQUIRED", RGB565_YELLOW);
      dsi_draw_text(px, 84, 372, 3, "RUN LABEL BEFORE DEFECT CHECK", RGB565_WHITE);
    }
  else if (defect_count < 0 && g_dsi_workflow_stage > 0)
    {
      dsi_draw_text(px, 84, 306, 4, "ACTIVE STEP", RGB565_WHITE);
      dsi_draw_text(px, 84, 364, 5,
                    dsi_workflow_stage_name(g_dsi_workflow_stage),
                    RGB565_CYAN);
    }
  else if (defect_count < 0)
    {
      dsi_draw_text(px, 84, 306, 3, "CAMERA NOT STARTED", RGB565_WHITE);
      dsi_draw_text(px, 84, 350, 3, "DSI UI ONLINE", RGB565_CYAN);
      dsi_draw_text(px, 84, 394, 3, "RUN LABEL INSPECTION DEMO", RGB565_WHITE);
    }
  else if (defect_count == 0)
    {
      dsi_draw_text(px, 84, 306, 3, "INFERENCE COMPLETE", RGB565_WHITE);
      dsi_draw_text(px, 84, 350, 3, "NO DEFECT CANDIDATE", RGB565_GREEN);
      dsi_draw_text(px, 84, 394, 3, "RESULT AWAITS VALIDATION", RGB565_CYAN);
    }
  else
    {
      snprintf(count_text, sizeof(count_text), "DEFECTS %d", defect_count);
      dsi_draw_text(px, 84, 306, 3, "INFERENCE COMPLETE", RGB565_WHITE);
      dsi_draw_text(px, 84, 350, 3, "DEFECT CANDIDATES", RGB565_YELLOW);
      dsi_draw_text(px, 84, 394, 3, count_text, RGB565_YELLOW);
    }

  if (energy_level >= 1 && energy_level <= 5)
    {
      snprintf(energy_text, sizeof(energy_text), "ENERGY LEVEL: %d", energy_level);
      dsi_draw_text(px, 84, 430, 3, energy_text, RGB565_GREEN);
    }
  else
    {
      dsi_draw_text(px, 84, 430, 3, "ENERGY LEVEL: --", RGB565_CYAN);
    }
}

static const char *dsi_workflow_stage_name(int stage)
{
  static const char *const names[] =
  {
    "READY", "CAPTURE", "GALLERY", "LABEL", "ENERGY",
    "STAIN", "DAMAGE", "WRINKLE", "POSITION"
  };

  return stage >= 0 && stage <= 8 ? names[stage] : names[0];
}

static void dsi_draw_workflow_button(uint16_t *px, int x, int y,
                                     const char *label, int stage,
                                     bool enabled)
{
  const bool selected = g_dsi_workflow_stage == stage;

  dsi_fill_rect(px, x, y, 214, 46, selected ? RGB565_CYAN :
                (enabled ? RGB565_BUTTON_NORMAL : RGB565_BUTTON_DISABLED));
  dsi_draw_text(px, x + 18, y + 14, 3, label,
                selected ? RGB565_BLACK :
                (enabled ? RGB565_WHITE : RGB565_CYAN));
}

static void dsi_draw_workflow_controls(uint16_t *px)
{
  const bool enabled = g_dsi_label_detected;

  dsi_draw_workflow_button(px, 48, 478, "CAPTURE", 1, true);
  dsi_draw_workflow_button(px, 286, 478, "GALLERY", 2, true);
  dsi_draw_workflow_button(px, 524, 478, "LABEL", 3, true);
  dsi_draw_workflow_button(px, 762, 478, "ENERGY", 4, enabled);
  dsi_draw_workflow_button(px, 48, 532, "STAIN", 5, enabled);
  dsi_draw_workflow_button(px, 286, 532, "DAMAGE", 6, enabled);
  dsi_draw_workflow_button(px, 524, 532, "WRINKLE", 7, enabled);
  dsi_draw_workflow_button(px, 762, 532, "POSITION", 8, enabled);
}

/* Redraw only the workflow area (stage name, inference result, buttons) after
 * a stage change, without tearing down and restarting the DSI display. */
static void dsi_redraw_workflow_area(void)
{
  uint16_t *px;

  if (!g_dsi_status_ui_active || g_dsi_status_ui_fb == NULL)
    {
      return;
    }

  px = (uint16_t *)g_dsi_status_ui_fb;

  /* Redraw stage name bar */
  dsi_fill_rect(px, 358, 196, 500, 42, RGB565_BUTTON_NORMAL);
  dsi_draw_text(px, 358, 196, 4,
                g_dsi_workflow_blocked ? "RUN LABEL FIRST" :
                dsi_workflow_stage_name(g_dsi_workflow_stage), RGB565_WHITE);

  /* Redraw inference result area */
  dsi_draw_inference_result(px, g_dsi_inference_result_count,
                            g_dsi_inference_energy_level);

  /* Redraw buttons with new selection state */
  dsi_draw_workflow_controls(px);
}

static void dsi_draw_status_ui(FAR uint8_t *fb)
{
  uint16_t *px = (uint16_t *)fb;

  dsi_fill_rect(px, 0, 0, PANEL_H_RES, PANEL_V_RES, RGB565_DARK_BLUE);
  dsi_fill_rect(px, 0, 0, PANEL_H_RES, 92, RGB565_TEAL);
  dsi_draw_text(px, 48, 23, 7, "OPENVELA", RGB565_BLACK);
  dsi_draw_text(px, 48, 61, 3, "ESP32-P4 / CAMERA INSPECTION", RGB565_BLACK);

  dsi_fill_rect(px, 48, 128, PANEL_H_RES - 96, 120, RGB565_PANEL_BLUE);
  dsi_draw_text(px, 84, 148, 4, "WORKFLOW", RGB565_WHITE);
  dsi_fill_rect(px, 84, 188, PANEL_H_RES - 168, 42, RGB565_BUTTON_NORMAL);
  dsi_draw_text(px, 108, 196, 3, "SELECTED", RGB565_CYAN);
  dsi_draw_text(px, 358, 196, 4,
                g_dsi_workflow_blocked ? "RUN LABEL FIRST" :
                dsi_workflow_stage_name(g_dsi_workflow_stage), RGB565_WHITE);

  dsi_draw_inference_result(px, g_dsi_inference_result_count,
                            g_dsi_inference_energy_level);
  dsi_draw_workflow_controls(px);

  printf("dsi: minimal status UI drawn (%dx%d RGB565)\n",
         PANEL_H_RES, PANEL_V_RES);
}

static void dsi_draw_pattern(FAR uint8_t *fb, int mode)
{
  uint16_t *px = (uint16_t *)fb;
  int x, y;

  if (mode == 0)
    {
      /* Solid red */
      for (y = 0; y < PANEL_V_RES; y++)
        {
          for (x = 0; x < PANEL_H_RES; x++)
            {
              px[y * PANEL_H_RES + x] = RGB565_RED;
            }
        }
    }
  else if (mode == 1)
    {
      /* Solid green */
      for (y = 0; y < PANEL_V_RES; y++)
        {
          for (x = 0; x < PANEL_H_RES; x++)
            {
              px[y * PANEL_H_RES + x] = RGB565_GREEN;
            }
        }
    }
  else if (mode == 4)
    {
      dsi_draw_status_ui(fb);
    }
  else
    {
      /* Vertical 8-color bar */
      static const uint16_t bar[8] =
        {
          RGB565_WHITE, RGB565_YELLOW, RGB565_CYAN, RGB565_GREEN,
          RGB565_MAGENTA, RGB565_RED, RGB565_BLUE, RGB565_BLACK,
        };
      int b;

      for (y = 0; y < PANEL_V_RES; y++)
        {
          for (x = 0; x < PANEL_H_RES; x++)
            {
              b = (x * 8) / PANEL_H_RES;
              px[y * PANEL_H_RES + x] = bar[b];
            }
        }
    }

  printf("dsi: pattern mode=%d drawn (%dx%d RGB565)\n",
         mode, PANEL_H_RES, PANEL_V_RES);
}

/* ------------------------------------------------------------------------
 * Public: --dsi-pattern entry
 * ---------------------------------------------------------------------- */

int esp32p4_dsi_pattern_diag(int mode, uint32_t hold_ms)
{
  FAR uint8_t *fb;
  size_t fb_bytes;
  bool host_pattern;
  bool persistent_ui;
  int ret;

  if (g_dsi_status_ui_active)
    {
      return mode == 4 ? OK : -EBUSY;
    }

  if (mode < 0 || mode > 4)
    {
      mode = 2;
    }

  host_pattern = mode == 3;
  persistent_ui = mode == 4 && hold_ms == UINT32_MAX;

  if (!persistent_ui && hold_ms == 0)
    {
      hold_ms = 5000;
    }

  fb_bytes = PANEL_H_RES * PANEL_V_RES * PANEL_BPP / 8;

  /* MIPI PHY LDO (chan 3, 2.5 V) - shared with CSI PHY */
  {
    esp_ldo_channel_config_t ldo_cfg;

    memset(&ldo_cfg, 0, sizeof(ldo_cfg));
    ldo_cfg.chan_id    = 3;
    ldo_cfg.voltage_mv = 2500;
    ret = esp_ldo_acquire_channel(&ldo_cfg, &g_dsi_ldo);
    if (ret != OK)
      {
        printf("dsi: LDO acquire failed: %d\n", ret);
        return -EIO;
      }

    g_dsi_ldo_acquired = true;
  }

  fb = NULL;

  /* Mode 3 follows the official panel-start lifecycle before it switches
   * from bridge output to the Host pattern generator. */
    {
      fb = (FAR uint8_t *)heap_caps_aligned_alloc(
          64, fb_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
      if (fb == NULL)
        {
          printf("dsi: fb alloc failed (%lu B)\n", (unsigned long)fb_bytes);
          ret = -ENOMEM;
          goto ldo_err;
        }

      printf("dsi: fb=%p bytes=%lu\n", fb, (unsigned long)fb_bytes);
    }

  /* Bus init + panel init */
  ret = dsi_bus_init();
  if (ret != OK)
    {
      printf("dsi: bus init failed: %d\n", ret);
      goto fb_err;
    }

  dsi_dpi_config();

  /* A previous host-pattern diagnostic can survive a soft application reset.
   * Normal UI mode must explicitly select framebuffer pixels. */
  if (!host_pattern)
    {
      mipi_dsi_host_ll_dpi_set_pattern_type(s_dsi_hal.host,
                                            MIPI_DSI_PATTERN_NONE);
    }

  dsi_board_panel_reset();
  dsi_panel_io_config();
  dsi_panel_send_init_cmds();
  dsi_board_backlight_enable();

  if (host_pattern)
    {
      memset(fb, 0, fb_bytes);
    }
  else
    {
      dsi_draw_pattern(fb, mode);
    }

  esp_cache_msync(fb, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  g_dsi_dma_dev = DW_GDMA_LL_GET_HW(0);
  ret = dsi_gdma_start(fb, fb_bytes);
  if (ret != OK)
    {
      printf("dsi: gdma start failed: %d\n", ret);
      goto fb_err;
    }

  /* Mirror dpi_panel_init(): video and bridge output precede set_pattern. */
  mipi_dsi_host_ll_enable_video_mode(s_dsi_hal.host, true);
  mipi_dsi_brg_ll_enable_dpi_output(s_dsi_hal.bridge, true);
  mipi_dsi_brg_ll_update_dpi_config(s_dsi_hal.bridge);
  mipi_dsi_brg_ll_enable_interrupt(s_dsi_hal.bridge,
                                   MIPI_DSI_BRG_LL_EVENT_UNDERRUN, true);
  if (host_pattern)
    {
      printf("dsi: host vertical-bar generator selected\n");
      mipi_dsi_brg_ll_enable_dpi_output(s_dsi_hal.bridge, false);
      mipi_dsi_brg_ll_update_dpi_config(s_dsi_hal.bridge);
      mipi_dsi_host_ll_dpi_set_pattern_type(
          s_dsi_hal.host, MIPI_DSI_PATTERN_BAR_VERTICAL);
    }
  dsi_host_phy_dump("start");

  printf("dsi: panel refresh started, holding %lu ms...\n",
         (unsigned long)hold_ms);

  /* L1.8.25f black-screen diagnosis (read-only): sample the data path right
   * after start and again before teardown. */
  {
    uint32_t gdma_tr0 =
      dw_gdma_ll_channel_get_trans_amount(g_dsi_dma_dev, DSI_GDMA_CHANNEL);
    uint32_t gdma_intr0 =
      dw_gdma_ll_channel_get_intr_status(g_dsi_dma_dev, DSI_GDMA_CHANNEL);
    uint32_t fifo0 = mipi_dsi_brg_ll_get_fifo_depth(s_dsi_hal.bridge);
    uint32_t fifo_stat = s_dsi_hal.bridge->fifo_flow_status.val;

    printf("dsi: diag@t0 gdma_tr=%lu gdma_intr=0x%08lx fifo_depth=%lu "
           "fifo_stat=0x%08lx\n",
           (unsigned long)gdma_tr0, (unsigned long)gdma_intr0,
           (unsigned long)fifo0, (unsigned long)fifo_stat);
    dsi_gdma_dump_lli();
  }

  if (persistent_ui)
    {
      g_dsi_status_ui_fb = fb;
      g_dsi_status_ui_active = true;
      printf("dsi: persistent status UI active\n");
      return OK;
    }

  up_mdelay(hold_ms);
  dsi_host_phy_dump("end");

  /* L1.8.25f black-screen diagnosis: second sample before teardown. */
  {
    uint32_t gdma_tr1 =
      dw_gdma_ll_channel_get_trans_amount(g_dsi_dma_dev, DSI_GDMA_CHANNEL);
    uint32_t gdma_intr1 =
      dw_gdma_ll_channel_get_intr_status(g_dsi_dma_dev, DSI_GDMA_CHANNEL);
    uint32_t fifo1 = mipi_dsi_brg_ll_get_fifo_depth(s_dsi_hal.bridge);
    uint32_t fifo_stat1 = s_dsi_hal.bridge->fifo_flow_status.val;

    printf("dsi: diag@t1 gdma_tr=%lu gdma_intr=0x%08lx fifo_depth=%lu "
           "fifo_stat=0x%08lx\n",
           (unsigned long)gdma_tr1, (unsigned long)gdma_intr1,
           (unsigned long)fifo1, (unsigned long)fifo_stat1);
  }

  /* Teardown */
  if (host_pattern)
    {
      mipi_dsi_host_ll_dpi_set_pattern_type(s_dsi_hal.host,
                                             MIPI_DSI_PATTERN_NONE);
    }

  mipi_dsi_brg_ll_enable_dpi_output(s_dsi_hal.bridge, false);
  mipi_dsi_brg_ll_update_dpi_config(s_dsi_hal.bridge);
  mipi_dsi_host_ll_enable_video_mode(s_dsi_hal.host, false);
  dsi_gdma_stop();

  printf("dsi: diag done\n");

  if (fb != NULL)
    {
      heap_caps_free(fb);
    }
  if (g_dsi_ldo_acquired)
    {
      esp_ldo_release_channel(g_dsi_ldo);
      g_dsi_ldo_acquired = false;
    }

  return OK;

fb_err:
  if (fb != NULL)
    {
      heap_caps_free(fb);
    }

ldo_err:
  if (g_dsi_ldo_acquired)
    {
      esp_ldo_release_channel(g_dsi_ldo);
      g_dsi_ldo_acquired = false;
    }

  return ret;
}

int esp32p4_dsi_status_ui_diag(uint32_t hold_ms)
{
  return esp32p4_dsi_pattern_diag(4, hold_ms);
}

int esp32p4_dsi_status_ui_start(void)
{
  return esp32p4_dsi_pattern_diag(4, UINT32_MAX);
}

bool esp32p4_dsi_status_ui_is_active(void)
{
  return g_dsi_status_ui_active;
}

int esp32p4_dsi_status_ui_set_inference_result(int candidate_count)
{
  return esp32p4_dsi_status_ui_set_inspection_result(candidate_count, -1);
}

int esp32p4_dsi_status_ui_set_inspection_result(int candidate_count,
                                                int energy_level)
{
  uint16_t *px;

  if (candidate_count < 0)
    {
      return -EINVAL;
    }

  g_dsi_inference_result_count = candidate_count;
  g_dsi_inference_energy_level = energy_level;
  if (!g_dsi_status_ui_active || g_dsi_status_ui_fb == NULL)
    {
      return OK;
    }

  /* The press renderer restores a saved region on release. Do not redraw
   * beneath an active press or it would restore stale pixels afterwards. */
  if (g_dsi_touch_has_point)
    {
      return -EBUSY;
    }

  px = (uint16_t *)g_dsi_status_ui_fb;
  dsi_draw_inference_result(px, candidate_count, energy_level);
  dsi_cache_sync_rect(g_dsi_status_ui_fb, 48, 278,
                      PANEL_H_RES - 49, 459);
  printf("dsi: inspection result candidates=%d energy_level=%d\n",
         candidate_count, energy_level);
  return OK;
}

int esp32p4_dsi_status_ui_set_label_detected(bool detected)
{
  if (g_dsi_label_detected == detected)
    return OK;

  g_dsi_label_detected = detected;
  if (!g_dsi_status_ui_active || g_dsi_status_ui_fb == NULL ||
      g_dsi_touch_has_point)
    return OK;

  dsi_draw_workflow_controls((uint16_t *)g_dsi_status_ui_fb);
  dsi_cache_sync_rect(g_dsi_status_ui_fb, 48, 478,
                      PANEL_H_RES - 49, 577);
  printf("dsi: workflow label_detected=%d\n", detected);
  return OK;
}

int esp32p4_dsi_status_ui_touch_feedback(uint16_t x, uint16_t y,
                                         unsigned int event_count,
                                         bool clear_trail)
{
  uint16_t *px;
  (void)event_count;
  (void)clear_trail;

  if (!g_dsi_status_ui_active || g_dsi_status_ui_fb == NULL)
    {
      return -ENODEV;
    }

  if (x >= PANEL_H_RES || y >= PANEL_V_RES)
    {
      return -EINVAL;
    }

  px = (uint16_t *)g_dsi_status_ui_fb;
  if (g_dsi_touch_has_point && g_dsi_touch_x == x && g_dsi_touch_y == y)
    {
      return OK;
    }

  if (g_dsi_touch_has_point)
    {
      dsi_touch_restore_region(px);
      dsi_cache_sync_rect(g_dsi_status_ui_fb, g_dsi_touch_x0,
                          g_dsi_touch_y0,
                          g_dsi_touch_x0 + g_dsi_touch_width - 1,
                          g_dsi_touch_y0 + g_dsi_touch_height - 1);
    }

  dsi_touch_backup_region(px, x, y);
  dsi_draw_touch_press(px, x, y);
  dsi_cache_sync_rect(g_dsi_status_ui_fb, g_dsi_touch_x0, g_dsi_touch_y0,
                      g_dsi_touch_x0 + g_dsi_touch_width - 1,
                      g_dsi_touch_y0 + g_dsi_touch_height - 1);
  dsi_submit_status_ui_frame();
  g_dsi_touch_has_point = true;
  g_dsi_touch_x = x;
  g_dsi_touch_y = y;
  printf("dsi: touch press x=%u y=%u\n",
         (unsigned int)x, (unsigned int)y);
  return OK;
}

void esp32p4_dsi_status_ui_touch_release(void)
{
  uint16_t *px;

  if (!g_dsi_status_ui_active || g_dsi_status_ui_fb == NULL ||
      !g_dsi_touch_has_point)
    {
      return;
    }

  px = (uint16_t *)g_dsi_status_ui_fb;
  dsi_touch_restore_region(px);
  dsi_cache_sync_rect(g_dsi_status_ui_fb, g_dsi_touch_x0, g_dsi_touch_y0,
                      g_dsi_touch_x0 + g_dsi_touch_width - 1,
                      g_dsi_touch_y0 + g_dsi_touch_height - 1);
  dsi_submit_status_ui_frame();
  g_dsi_touch_has_point = false;
  if (g_dsi_touch_y >= 478 && g_dsi_touch_y < 578)
    {
      int column = (g_dsi_touch_x - 48) / 238;
      int stage = (g_dsi_touch_y >= 532 ? 4 : 0) + column + 1;
      if (column >= 0 && column < 4)
        {
          if (stage > 3 && !g_dsi_label_detected)
            {
              g_dsi_workflow_blocked = true;
              printf("dsi: workflow stage=%d blocked: label required\n",
                     stage);
            }
          else
            {
              g_dsi_workflow_blocked = false;
              g_dsi_workflow_stage = stage;
              printf("dsi: workflow stage=%d\n", stage);
              if (g_dsi_workflow_cb)
                {
                  g_dsi_workflow_cb(stage);
                }
}

          /* Redraw the workflow area (buttons, stage name, inference)
           * in-place and submit via cache sync. No stop/start cycle. */
          dsi_redraw_workflow_area();
          dsi_submit_status_ui_frame();
        }
    }
  printf("dsi: touch release restored UI\n");
}

void esp32p4_dsi_status_ui_stop(void)
{
  if (!g_dsi_status_ui_active)
    {
      return;
    }

  mipi_dsi_brg_ll_enable_dpi_output(s_dsi_hal.bridge, false);
  mipi_dsi_brg_ll_update_dpi_config(s_dsi_hal.bridge);
  mipi_dsi_host_ll_enable_video_mode(s_dsi_hal.host, false);
  dsi_gdma_stop();
  heap_caps_free(g_dsi_status_ui_fb);
  g_dsi_status_ui_fb = NULL;
  g_dsi_status_ui_active = false;
  g_dsi_touch_has_point = false;

  if (g_dsi_ldo_acquired)
    {
      esp_ldo_release_channel(g_dsi_ldo);
      g_dsi_ldo_acquired = false;
    }

  printf("dsi: persistent status UI stopped\n");
}

static int dsi_rgb565_session_start(FAR uint8_t *frame, size_t frame_bytes,
                                    bool shared_gdma_irq)
{
  const size_t expected_bytes = PANEL_H_RES * PANEL_V_RES * PANEL_BPP / 8;
  int ret;

  if (frame == NULL || frame_bytes != expected_bytes)
    {
      return -EINVAL;
    }

  {
    esp_ldo_channel_config_t ldo_cfg;

    memset(&ldo_cfg, 0, sizeof(ldo_cfg));
    ldo_cfg.chan_id = 3;
    ldo_cfg.voltage_mv = 2500;
    ret = esp_ldo_acquire_channel(&ldo_cfg, &g_dsi_ldo);
    if (ret != OK)
      {
        return -EIO;
      }

    g_dsi_ldo_acquired = true;
  }

  dsi_board_gpio_prepare();
  ret = dsi_bus_init();
  if (ret != OK)
    {
      goto ldo_err;
    }

  dsi_dpi_config();
  dsi_board_panel_reset();
  dsi_panel_io_config();
  dsi_panel_send_init_cmds();
  dsi_board_backlight_enable();

  esp_cache_msync(frame, frame_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  g_dsi_dma_dev = DW_GDMA_LL_GET_HW(0);

  /* CSI has already initialized the shared controller for live preview.
   * Do not reset it here, or channel 0 would lose its capture state. */
  if (shared_gdma_irq)
    {
      g_dsi_gdma_controller_init = true;
    }

  g_dsi_gdma_shared_irq = shared_gdma_irq;

  ret = dsi_gdma_start(frame, frame_bytes);
  if (ret != OK)
    {
      goto ldo_err;
    }

  mipi_dsi_host_ll_enable_video_mode(s_dsi_hal.host, true);
  mipi_dsi_brg_ll_enable_dpi_output(s_dsi_hal.bridge, true);
  mipi_dsi_brg_ll_update_dpi_config(s_dsi_hal.bridge);

  return OK;

ldo_err:
  if (g_dsi_ldo_acquired)
    {
      esp_ldo_release_channel(g_dsi_ldo);
      g_dsi_ldo_acquired = false;
    }

  return ret;
}

static void dsi_rgb565_session_stop(void)
{

  mipi_dsi_brg_ll_enable_dpi_output(s_dsi_hal.bridge, false);
  mipi_dsi_brg_ll_update_dpi_config(s_dsi_hal.bridge);
  mipi_dsi_host_ll_enable_video_mode(s_dsi_hal.host, false);
  dsi_gdma_stop();
  g_dsi_gdma_shared_irq = false;

  if (g_dsi_ldo_acquired)
    {
      esp_ldo_release_channel(g_dsi_ldo);
      g_dsi_ldo_acquired = false;
    }
}

int esp32p4_dsi_show_rgb565(FAR uint8_t *frame, size_t frame_bytes,
                             uint32_t hold_ms)
{
  int ret;

  if (hold_ms == 0)
    {
      hold_ms = 5000;
    }

  ret = dsi_rgb565_session_start(frame, frame_bytes, false);
  if (ret != OK)
    {
      return ret;
    }

  printf("dsi: RGB565 frame preview holding %lu ms\n",
         (unsigned long)hold_ms);
  up_mdelay(hold_ms);
  dsi_rgb565_session_stop();

  return OK;
}

int esp32p4_dsi_live_start(FAR uint8_t *frame, size_t frame_bytes)
{
  return dsi_rgb565_session_start(frame, frame_bytes, true);
}

void esp32p4_dsi_live_stop(void)
{
  dsi_rgb565_session_stop();
}

void esp32p4_dsi_set_workflow_callback(esp32p4_dsi_workflow_cb_t cb)
{
  g_dsi_workflow_cb = cb;
}