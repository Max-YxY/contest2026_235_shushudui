/****************************************************************************
 * apps/examples/camera_diag/esp32p4_csi.c
 *
 * S11-L1.6 camera build integration: minimal ESP32-P4 MIPI-CSI receive path
 * with DW-GDMA frame buffer (RAW8 1280x720). References:
 *   - esp-hal-3rdparty mipi_csi_hal/ll (built into the image, L1-A)
 *   - esp-hal-3rdparty dw_gdma_ll (channel 0, src=CSI bridge, dst=memory)
 *   - Espressif esp_cam_ctlr_csi.c DW-GDMA flow (contiguous P2M, src flow)
 *
 * Scope (per L1.6 authorization): CSI receive config, GDMA frame buffer,
 * timeout/cleanup, camera_diag metadata output. No ISP full pipeline,
 * no NuttX video/V4L2, no display preview, no image save, no algorithm.
 * Build-level integration only; no hardware operation performed.
 *
 * Controlled camera diagnostic; not real label inspection.
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>   /* up_udelay / up_enable_irq declarations */

#include <errno.h>
#include <stdio.h>
#include <debug.h>

#include "irq.h"        /* DW_GDMA_INTR_SOURCE / ESP_IRQ_DW_GDMA / ESP_SOURCE2IRQ */
#include "esp_irq.h"   /* esp_setup_irq / esp_teardown_irq / priorities */

#include "esp32p4_csi.h"
#include "esp32p4_csi_isp.h"
#include "esp32p4_dsi.h"
#include "camera_diag_gdma_plan.h"
#include "esp_ldo_regulator.h"
#include "esp_private/mipi_csi_share_hw_ctrl.h"

/* ESP-IDF RCC atomic-env helper shim: expands to the variable name that
 * callers declare before using the clock-gate ll wrappers (build-level). */
#define __DECLARE_RCC_ATOMIC_ENV rcc_atomic_env

#include "hal/mipi_csi_hal.h"
#include "hal/mipi_csi_ll.h"
#include "hal/mipi_csi_brg_ll.h"
#include "hal/mipi_csi_host_ll.h"
#include "hal/dw_gdma_ll.h"
#include "soc/reg_base.h"
#include "esp_cache.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

#define CSI_DMA_GROUP    0
#define CSI_DMA_CHANNEL  0

static mipi_csi_hal_context_t g_csi_hal;
static dw_gdma_dev_t *g_dma_dev;
static struct esp32p4_csi_config_s g_csi_cfg;

/* L1.8.14 F1: MIPI CSI/DSI PHY power LDO (channel 3, 2.5 V), required before
 * CSI bridge/PHY init (ESP-IDF mipi_isp_dsi example + test_csi_ov5647). */
#define CSI_MIPI_PHY_LDO_CHAN   3
#define CSI_MIPI_PHY_LDO_MV     2500
static esp_ldo_channel_handle_t g_csi_ldo;
static bool g_csi_ldo_acquired;

/* L1.8.25f R2: MIPI CSI bridge claim state (MIPI_CSI_BRG_USER_CSI). The
 * bridge MUST be claimed by the CSI controller BEFORE the ISP SHARE claim,
 * otherwise the ISP's first claim resets the bridge module clock and wipes
 * the configuration (burst_len=512/afull=960/data_type) back to reset
 * defaults (official success-path baseline, verified 3/3 on target). */
static bool g_csi_brg_claimed;
static int  g_csi_brg_id = -1;

/* L1.8.25f R3-P2: DW-GDMA block-done ISR state (ISP/RGB565 path only).
 * In SRC (bridge) flow-controller mode the GDMA block completes on the
 * bridge frame-boundary signal, NOT on the block_ts item count, and the
 * status0.cmpltd_blk_tfr_size read-back saturates (NuttX observed 960 =
 * MAX_BLK_SIZE) - so polling trans_amount >= frame_items can never succeed.
 * The fix registers a BLOCK_TFR_DONE interrupt; the ISR (interrupt context:
 * no logging/locks/libc) clears the event, bumps a volatile block counter
 * and re-arms the next block; the task context aggregates block completions
 * into whole RGB565 frames. */
static volatile uint32_t g_csi_block_count;    /* ISR-write, task-read */
/* L1.8.25f R4-D: cumulative actual completed items across block-done
 * events (sum of status0.cmpltd_blk_tfr_size readbacks). BLOCK_TFR_DONE
 * fires at bridge FIFO watermarks (960 items saturating), NOT at whole
 * frame size; a frame is complete only when the accumulated item count
 * reaches frame_items (153600 = 1228800 B / 8 B). */
static volatile uint32_t g_csi_frame_items_done; /* ISR-write, task-read */
/* L1.8.25f R3-P3 (read-only diag): ISR entry counter + last int_st0 seen by
 * the ISR, to distinguish "IRQ never routed" (entry_count==0) from
 * "block-done is a teardown artifact" (entry_count>0 but no block event). */
static volatile uint32_t g_csi_isr_entry_count;  /* every ISR entry */
static volatile uint32_t g_csi_isr_last_intr;    /* int_st0 snapshot in ISR */
static volatile bool     g_csi_block_armed;    /* ISR re-arm in progress */
static volatile uint32_t g_csi_isr_items;      /* per-block item count for re-arm */
static volatile uint32_t g_csi_isr_dst;        /* destination addr for re-arm */
static bool              g_csi_block_isr_attached; /* esp_setup_irq state */
static int               g_csi_block_cpuint = -1;   /* cpuint from esp_setup_irq */
static bool              g_csi_runtime_diag_enabled = true;
#define CSI_FRAME_BLOCKS_PER_FRAME 1u          /* legacy: frame completion now judged on g_csi_frame_items_done >= frame_items (153600), NOT on block-done count (R4-D) */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void esp32p4_csi_dma_channel_config(void)
{
  dw_gdma_dev_t *dev = g_dma_dev;
  uint8_t ch = CSI_DMA_CHANNEL;

  dw_gdma_ll_channel_set_trans_flow(dev, ch, DW_GDMA_ROLE_PERIPH_CSI,
                                    DW_GDMA_ROLE_MEM, DW_GDMA_FLOW_CTRL_SRC);
  dw_gdma_ll_channel_set_src_handshake_interface(dev, ch, DW_GDMA_HANDSHAKE_HW);
  dw_gdma_ll_channel_set_src_handshake_periph(dev, ch, DW_GDMA_ROLE_PERIPH_CSI);
  dw_gdma_ll_channel_set_dst_handshake_interface(dev, ch, DW_GDMA_HANDSHAKE_HW);

  dw_gdma_ll_channel_set_src_burst_mode(dev, ch, DW_GDMA_BURST_MODE_FIXED);
  dw_gdma_ll_channel_set_src_burst_items(dev, ch, DW_GDMA_BURST_ITEMS_512);
  dw_gdma_ll_channel_set_src_burst_len(dev, ch, 16);
  dw_gdma_ll_channel_set_src_trans_width(dev, ch, DW_GDMA_TRANS_WIDTH_64);

  dw_gdma_ll_channel_set_dst_burst_mode(dev, ch, DW_GDMA_BURST_MODE_INCREMENT);
  dw_gdma_ll_channel_set_dst_burst_items(dev, ch, DW_GDMA_BURST_ITEMS_512);
  dw_gdma_ll_channel_set_dst_burst_len(dev, ch, 16);
  dw_gdma_ll_channel_set_dst_trans_width(dev, ch, DW_GDMA_TRANS_WIDTH_64);

  dw_gdma_ll_channel_set_src_multi_block_type(dev, ch,
                                              DW_GDMA_BLOCK_TRANSFER_CONTIGUOUS);
  dw_gdma_ll_channel_set_dst_multi_block_type(dev, ch,
                                              DW_GDMA_BLOCK_TRANSFER_CONTIGUOUS);
  dw_gdma_ll_channel_set_src_outstanding_limit(dev, ch, 5);
  dw_gdma_ll_channel_set_dst_outstanding_limit(dev, ch, 5);
  dw_gdma_ll_channel_set_priority(dev, ch, 1);

  dw_gdma_ll_channel_set_src_master_port(dev, ch, MIPI_CSI_BRG_MEM_BASE);
  dw_gdma_ll_channel_set_src_periph_status_addr(dev, ch, MIPI_CSI_BRG_MEM_BASE);
  dw_gdma_ll_channel_enable_src_periph_status_write_back(dev, ch, true);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* L1.8.8: CSI bridge/host init only (no GDMA). Used by --csi-runtime-diag so
 * the D1 (block size) / D2 (enable order) defects do not interfere with the
 * MIPI HS/PHY observation. */
int esp32p4_csi_brg_init(FAR const struct esp32p4_csi_config_s *cfg)
{
  mipi_csi_hal_config_t hal_cfg;
  esp_ldo_channel_config_t ldo_cfg;
  int rcc_atomic_env;          /* __DECLARE_RCC_ATOMIC_ENV shim */
  int ret;

  if (cfg == NULL)
    {
      return -EINVAL;
    }

  g_csi_cfg = *cfg;

  /* L1.8.25f R2: claim the MIPI CSI bridge as the CSI user FIRST, before
   * any bridge configuration. The official esp_cam_ctlr_csi.c does exactly
   * this (mipi_csi_brg_claim(MIPI_CSI_BRG_USER_CSI)); the ISP SHARE claim
   * then increments the ref-count WITHOUT resetting the bridge module
   * clock, so the configuration written below survives. Without this claim
   * the ISP's first claim runs mipi_csi_ll_reset_brg_module_clock() and
   * wipes burst_len=512/afull=960/data_type back to reset defaults (the
   * NuttX failure root cause, confirmed against the official 3/3 baseline).
   * Guarded by g_csi_brg_claimed to make repeated init idempotent. */
  if (!g_csi_brg_claimed)
    {
      int csi_brg_id = -1;
      ret = mipi_csi_brg_claim(MIPI_CSI_BRG_USER_CSI, &csi_brg_id);
      if (ret != OK)
        {
          i2cerr("csi: bridge claim (CSI) failed: %d\n", ret);
          return -EIO;
        }

      g_csi_brg_id = csi_brg_id;
      g_csi_brg_claimed = true;
    }

  /* L1.8.14 F1: power up the MIPI CSI/DSI PHY before any CSI bridge/PHY
   * init (mirrors official ESP-IDF: esp_ldo_acquire_channel chan 3, 2.5 V).
   * On failure the CSI bridge claim is released symmetrically. */
  if (!g_csi_ldo_acquired)
    {
      memset(&ldo_cfg, 0, sizeof(ldo_cfg));
      ldo_cfg.chan_id    = CSI_MIPI_PHY_LDO_CHAN;
      ldo_cfg.voltage_mv = CSI_MIPI_PHY_LDO_MV;
      ret = esp_ldo_acquire_channel(&ldo_cfg, &g_csi_ldo);
      if (ret != OK)
        {
          if (g_csi_brg_claimed)
            {
              mipi_csi_brg_declaim(g_csi_brg_id);
              g_csi_brg_claimed = false;
              g_csi_brg_id = -1;
            }

          return -EIO;
        }

      g_csi_ldo_acquired = true;
    }

  /* Enable module clocks (bridge / phy config / host) */
  mipi_csi_ll_enable_brg_module_clock(0, true);
  mipi_csi_ll_reset_brg_module_clock(0);
  mipi_csi_ll_enable_phy_config_clock(0, true);
  _mipi_csi_ll_enable_host_bus_clock(0, true);
  mipi_csi_ll_reset_host_clock(0);

  hal_cfg.lanes_num         = cfg->lanes_num;
  hal_cfg.frame_width       = cfg->frame_height;  /* L1.8.21 D1: hal v_row_num uses frame_width -> height */
  hal_cfg.frame_height      = cfg->frame_width;   /* L1.8.21 D1: hal h_pixel_num uses frame_height -> width */
  hal_cfg.in_bpp            = cfg->in_bpp;
  hal_cfg.out_bpp           = cfg->out_bpp;
  hal_cfg.byte_swap_en      = cfg->byte_swap_en;
  hal_cfg.lane_bit_rate_mbps = cfg->lane_bit_rate_mbps;

  mipi_csi_hal_init(&g_csi_hal, &hal_cfg);

  /* CSI bridge: burst + clock + RAW8 bypass */
  mipi_csi_brg_ll_set_burst_len(g_csi_hal.bridge_dev, 512);
  mipi_csi_brg_ll_set_dma_req_interval(g_csi_hal.bridge_dev, 1);
  mipi_csi_brg_ll_set_color_mode_bypass(g_csi_hal.bridge_dev, true);
  mipi_csi_brg_ll_enable_clock(g_csi_hal.bridge_dev, true);

  return OK;
}

int esp32p4_csi_init(FAR const struct esp32p4_csi_config_s *cfg)
{
  int rcc_atomic_env;          /* __DECLARE_RCC_ATOMIC_ENV shim (dw_gdma_ll) */
  int ret;

  ret = esp32p4_csi_brg_init(cfg);
  if (ret != OK)
    {
      return ret;
    }

  /* DW-GDMA: clock, reset, controller, channel 0 = CSI P2M */
  g_dma_dev = DW_GDMA_LL_GET_HW(CSI_DMA_GROUP);
  if (g_dma_dev == NULL)
    {
      /* L1.8.25f R2: symmetric release of the bridge claim taken inside
       * esp32p4_csi_brg_init() before this failure is returned. */
      if (g_csi_brg_claimed)
        {
          mipi_csi_brg_declaim(g_csi_brg_id);
          g_csi_brg_claimed = false;
          g_csi_brg_id = -1;
        }

      /* L1.8.14 F1: release the MIPI PHY LDO on init failure. */
      if (g_csi_ldo_acquired)
        {
          esp_ldo_release_channel(g_csi_ldo);
          g_csi_ldo_acquired = false;
        }

      return -ENODEV;
    }

  dw_gdma_ll_enable_bus_clock(CSI_DMA_GROUP, true);
  dw_gdma_ll_reset_register(CSI_DMA_GROUP);
  dw_gdma_ll_reset(g_dma_dev);
  dw_gdma_ll_enable_controller(g_dma_dev, true);
  /* L1.8.25f R4-D: module-level global interrupt-output gate. Mirrors the
   * official dw_gdma_hal_init() third step (esp_hal_dma/dw_gdma_hal.c):
   * without cfg0.int_en=1 the GDMA interrupt signal never leaves the
   * module toward the INTC, so CLIC pending stays 0 and the ISR never
   * runs even though channel events are produced (R4-C clic_ip=0 root). */
  dw_gdma_ll_enable_intr_global(g_dma_dev, true);
  esp32p4_csi_dma_channel_config();

  i2cinfo("csi: lanes=%u %ux%u in_bpp=%u out_bpp=%u lane_rate=%d Mbps dma=ch%u\n",
          cfg->lanes_num, cfg->frame_width, cfg->frame_height,
          cfg->in_bpp, cfg->out_bpp, cfg->lane_bit_rate_mbps,
          CSI_DMA_CHANNEL);
  return OK;
}

int esp32p4_csi_enable(void)
{
  /* Bridge/dma enable happens per capture (see capture_one). */
  return OK;
}

/****************************************************************************
 * Name: esp32p4_csi_block_done_isr
 *
 * Description:
 *   L1.8.25f R3-P2: DW-GDMA BLOCK_TFR_DONE interrupt service routine
 *   (ISP/RGB565 path only). Interrupt context: NO logging, NO locks, NO
 *   libc. On a block-done event it clears the event bit, bumps the volatile
 *   block counter, and re-arms the next block (mirrors the official
 *   csi_dma_trans_done_callback re-arm: config_transfer + enable_ctrl(true)).
 *   With is_last=false the channel stays enabled; the task context decides
 *   when a full RGB565 frame (1 block = 153600 items) has completed.
 *
 * Returned Value:
 *   NuttX xcpt_t convention: 0 (no context switch requested).
 ****************************************************************************/

static int esp32p4_csi_block_done_isr(int irq, FAR void *arg, FAR void *context)
{
  dw_gdma_dev_t *dev = g_dma_dev;
  uint8_t ch = CSI_DMA_CHANNEL;
  uint32_t st;

  if (dev == NULL)
    {
      return 0;
    }

  st = dw_gdma_ll_channel_get_intr_status(dev, ch);
  /* ch0 (CSI) and ch1 (DSI) share DW_GDMA_INTR_SOURCE.  Service ch1 first
   * when a live DSI session is active; it only reads/clears channel 1 and
   * re-arms its own LLI. */
  esp32p4_dsi_gdma_irq_handler();
  g_csi_isr_entry_count++;          /* R3-P3: ISR entered (routing proof) */
  g_csi_isr_last_intr = st;         /* R3-P3: int_st0 as seen by the ISR */

  /* L1.8.25f R4-D2: clear ALL latched status bits on entry, mirroring the
   * official dw_gdma_channel_default_isr() (read full int_st0 -> clear all
   * -> dispatch). int_sig_ena0 defaults to all-ones, so non-BLOCK events
   * (SRC_TRANSCOMP etc.) also reach the CLIC once the module-level gate is
   * open; leaving them latched would keep the pending bit asserted and
   * re-trigger the ISR forever (interrupt storm -> UART stall / hang). */
  dw_gdma_ll_channel_clear_intr(dev, ch, st);

  if (st & DW_GDMA_LL_CHANNEL_EVENT_BLOCK_TFR_DONE)
    {
      g_csi_block_count++;

      /* L1.8.25f R4-D: accumulate the actual completed items of this block
       * (cmpltd_blk_tfr_size). Do NOT treat a single block-done as a frame:
       * with SRC flow-control the event fires at bridge FIFO watermarks
       * (960-item saturation), not at the 153600-item frame boundary. */
      g_csi_frame_items_done += dw_gdma_ll_channel_get_trans_amount(dev, ch);

      if (g_csi_block_armed)
        {
          /* Re-arm the next block: source = bridge FIFO, dst = current
           * frame buffer, same block size, is_last=false (keep draining). */
          dw_gdma_ll_channel_set_src_addr(dev, ch, MIPI_CSI_BRG_MEM_BASE);
          dw_gdma_ll_channel_set_dst_addr(dev, ch, g_csi_isr_dst);
          dw_gdma_ll_channel_set_dst_master_port(dev, ch, (intptr_t)g_csi_isr_dst);
          dw_gdma_ll_channel_set_trans_block_size(dev, ch, g_csi_isr_items);
          dw_gdma_ll_channel_set_block_markers(dev, ch, true, false, true);
          dw_gdma_ll_channel_enable(dev, ch, true);
        }
    }

  return 0;
}

/****************************************************************************
 * Name: esp32p4_csi_install_block_isr
 *
 * Description:
 *   L1.8.25f R3-P2: install the DW-GDMA BLOCK_TFR_DONE interrupt for the
 *   ISP/RGB565 capture path. Called from task context (--isp-frame) after
 *   the ISP processor is enabled. Idempotent.
 *
 * Returned Value:
 *   OK (0) on success; negative errno on failure.
 ****************************************************************************/

int esp32p4_csi_install_block_isr(void)
{
  dw_gdma_dev_t *dev = g_dma_dev;
  uint8_t ch = CSI_DMA_CHANNEL;
  int ret;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  if (g_csi_block_isr_attached)
    {
      return OK;
    }

  /* L1.8.25f R4-D2: narrow the interrupt masks to the completion events we
   * actually consume, mirroring the official dw_gdma_install_channel_
   * interrupt() + register_event_callbacks() flow:
   *   1) disable propagation of ALL channel events, clear all latched status,
   *   2) re-enable generation + propagation ONLY for BLOCK_TFR_DONE.
   * int_sig_ena0 defaults to all-ones, so without this narrowing every
   * non-completion event (SRC_TRANSCOMP, errors, ch_disabled/aborted) would
   * keep entering the CLIC and, with the module gate open (R4-D), re-trigger
   * the ISR in a storm even though the ISR now clears all latched bits. */
  dw_gdma_ll_channel_enable_intr_propagation(dev, ch, 0xffffffffu, false);
  dw_gdma_ll_channel_clear_intr(dev, ch, 0xffffffffu);

  dw_gdma_ll_channel_enable_intr_generation(dev, ch,
                                            DW_GDMA_LL_CHANNEL_EVENT_BLOCK_TFR_DONE,
                                            true);
  dw_gdma_ll_channel_enable_intr_propagation(dev, ch,
                                             DW_GDMA_LL_CHANNEL_EVENT_BLOCK_TFR_DONE,
                                             true);

  /* NuttX esp32p4 peripheral interrupts are dispatched through the
   * esp_isr_demultiplexing handler; the handler must be registered with
   * esp_setup_irq (NOT raw irq_attach - that leaves the cpuint unmapped and
   * up_enable_irq reports "IRQ not mapped"). esp_setup_irq allocates a CPU
   * interrupt and maps DW_GDMA_INTR_SOURCE to it. */
  ret = esp_setup_irq(DW_GDMA_INTR_SOURCE, ESP_IRQ_PRIORITY_DEFAULT,
                      ESP_IRQ_TRIGGER_LEVEL, esp32p4_csi_block_done_isr,
                      NULL);
  if (ret < 0)
    {
      i2cerr("csi: esp_setup_irq(DW_GDMA) failed: %d\n", ret);
      return -EIO;
    }

  g_csi_block_cpuint = ret;
  up_enable_irq(ESP_SOURCE2IRQ(DW_GDMA_INTR_SOURCE));
  g_csi_block_isr_attached = true;
  i2cinfo("csi: block-done ISR installed (source=%d cpuint=%d)\n",
          DW_GDMA_INTR_SOURCE, g_csi_block_cpuint);
  return OK;
}

void esp32p4_csi_set_runtime_diag(bool enabled)
{
  g_csi_runtime_diag_enabled = enabled;
}

int esp32p4_csi_disable(void)
{

  /* L1.8.25f R2: release the CSI bridge claim exactly once (idempotent).
   * ref_cnt: CSI claim (1) + ISP SHARE claim (1) = 2 -> this declaim makes
   * it 1; the ISP declaim (inside esp_isp_del_processor) makes it 0. For
   * the RAW8 path (no ISP) this single declaim brings 1 -> 0. Repeated
   * disable() calls are no-ops and never underflow the ref-count. */
  if (g_csi_brg_claimed)
    {
      mipi_csi_brg_declaim(g_csi_brg_id);
      g_csi_brg_claimed = false;
      g_csi_brg_id = -1;
    }

  /* L1.8.25f R3-P2: detach the block-done ISR (idempotent). */
  if (g_csi_block_isr_attached)
    {
      esp_teardown_irq(DW_GDMA_INTR_SOURCE, g_csi_block_cpuint);
      g_csi_block_isr_attached = false;
      g_csi_block_cpuint = -1;
      g_csi_block_count = 0;
      g_csi_frame_items_done = 0;
      g_csi_block_armed = false;
    }

  /* L1.8.14 F1: release the MIPI PHY LDO on the shutdown path. */
  if (g_csi_ldo_acquired)
    {
      esp_ldo_release_channel(g_csi_ldo);
      g_csi_ldo_acquired = false;
    }

  return OK;
}

/****************************************************************************
 * Name: esp32p4_csi_brg_set_rgb565_out
 *
 * Description:
 *   L1.8.25e Stage B (ISP path): switch the CSI bridge data path to RGB565
 *   output. Mirrors the official esp_cam_ctlr_csi.c s_csi_ctlr_format_conversion
 *   for src==dst (RGB565 in/out): color conversion enabled, mode bypass set
 *   (input flows to output unchanged), both bridge input/output color formats
 *   set to RGB565. ISP keeps RAW8 input (demosaic to RGB565).
 *
 * Returned Value:
 *   OK (0) on success.
 ****************************************************************************/

int esp32p4_csi_brg_set_rgb565_out(void)
{
  /* Align with official: mipi_csi_brg_ll_enable_color_conversion(brg, true) is
   * set once at controller init; here we only configure the color format and
   * keep bypass (src == dst) per s_csi_ctlr_format_conversion. */
  mipi_csi_brg_ll_set_input_color_format(g_csi_hal.bridge_dev,
                                         CAM_CTLR_COLOR_RGB565);
  mipi_csi_brg_ll_set_output_color_format(g_csi_hal.bridge_dev,
                                          CAM_CTLR_COLOR_RGB565);
  mipi_csi_brg_ll_set_color_mode_bypass(g_csi_hal.bridge_dev, true);
  /* Official esp_cam_ctlr_csi.c:221 - color conversion enabled on the
   * bridge; bypass is set separately for the src==dst RGB565 path. */
  mipi_csi_brg_ll_enable_color_conversion(g_csi_hal.bridge_dev, true);

  i2cinfo("csi: bridge data path set to RGB565 (bypass, ISP output)\n");
  return OK;
}

/****************************************************************************
 * Name: esp32p4_csi_bridge_enable
 *
 * Description:
 *   Enable the MIPI CSI bridge once. In the ISP path this is done BEFORE
 *   the frame loop and the bridge stays enabled until the loop ends,
 *   mirroring the official esp_cam_ctlr_start lifecycle (the ISP reads
 *   RAW8 from the bridge continuously via mipi_data_en).
 *
 * Returned Value:
 *   OK (0).
 ****************************************************************************/

int esp32p4_csi_bridge_enable(void)
{
  mipi_csi_brg_ll_enable(g_csi_hal.bridge_dev, true);
  return OK;
}

/****************************************************************************
 * Name: esp32p4_csi_bridge_disable
 *
 * Description:
 *   Disable the MIPI CSI bridge once, after the frame loop or on failure.
 *
 * Returned Value:
 *   OK (0).
 ****************************************************************************/

int esp32p4_csi_bridge_disable(void)
{
  mipi_csi_brg_ll_enable(g_csi_hal.bridge_dev, false);
  return OK;
}

/****************************************************************************
 * Name: esp32p4_csi_isp_dump
 *
 * Description:
 *   L1.8.25e (ISP path, read-only runtime dump): dump the CSI bridge, host,
 *   DW-GDMA and ISP control register state. Used by --isp-frame at three
 *   points (after start / after first-frame timeout / before cleanup) to
 *   distinguish "CSI/ISP did not feed data into the bridge" from
 *   "bridge has data but the GDMA handshake did not advance".
 *   Read-only: no register is modified.
 *
 * Input Parameters:
 *   tag - Dump label printed as prefix (e.g. "START", "TIMEOUT", "CLEANUP").
 ****************************************************************************/

void esp32p4_csi_isp_dump(FAR const char *tag)
{
  csi_brg_dev_t *brg = g_csi_hal.bridge_dev;
  csi_host_dev_t *host = g_csi_hal.host_dev;
  dw_gdma_dev_t *dev = g_dma_dev;
  uint8_t ch = CSI_DMA_CHANNEL;
  uint32_t isp_cntl = esp32p4_csi_isp_get_cntl();

  /* ISP cntl register bit layout (soc/esp32p4 .../isp_struct.h):
   *   mipi_data_en bit0, isp_en bit1, isp_in_src bit[28:27],
   *   isp_out_type bit[31:29] (4 = RGB565). */
  printf("csi_dump[%s]: isp_cntl=0x%08lx (mipi_data_en=%u isp_en=%u "
         "in_src=%u out_type=%u)\n",
         tag, (unsigned long)isp_cntl,
         (unsigned int)((isp_cntl >> 0) & 0x1u),
         (unsigned int)((isp_cntl >> 1) & 0x1u),
         (unsigned int)((isp_cntl >> 27) & 0x3u),
         (unsigned int)((isp_cntl >> 29) & 0x7u));

  if (brg == NULL)
    {
      printf("csi_dump[%s]: bridge not initialized\n", tag);
      return;
    }

  printf("csi_dump[%s]: bridge en=%u clk=%u frame_cfg=0x%08lx dt=0x%08lx "
         "cm=0x%08lx bypass=%u burst_len=%lu req_int=%lu dmablk=%lu "
         "flow_ctrl=%u afull_thrd=%lu depth=%lu\n",
         tag,
         (unsigned int)brg->csi_en.csi_brg_en,
         (unsigned int)brg->host_ctrl.csi_enableclk,
         (unsigned long)brg->frame_cfg.val,
         (unsigned long)brg->data_type_cfg.val,
         (unsigned long)brg->host_cm_ctrl.val,
         (unsigned int)brg->host_cm_ctrl.csi_host_cm_bypass,
         (unsigned long)brg->dma_req_cfg.dma_burst_len,
         (unsigned long)brg->dma_req_interval.dma_req_interval,
         (unsigned long)brg->dmablk_size.dmablk_size,
         (unsigned int)brg->dma_req_cfg.csi_dma_flow_controller,
         (unsigned long)brg->buf_flow_ctl.csi_buf_afull_thrd,
         (unsigned long)brg->buf_flow_ctl.csi_buf_depth);

  printf("csi_dump[%s]: bridge int_raw=0x%08lx int_st=0x%08lx "
         "(vadr_gt=%u vadr_lt=%u discard=%u overrun=%u fifo_ovf=%u "
         "dma_upd=%u)\n",
         tag,
         (unsigned long)brg->int_raw.val,
         (unsigned long)brg->int_st.val,
         (unsigned int)brg->int_st.vadr_num_gt_int_st,
         (unsigned int)brg->int_st.vadr_num_lt_int_st,
         (unsigned int)brg->int_st.discard_int_st,
         (unsigned int)brg->int_st.csi_buf_overrun_int_st,
         (unsigned int)brg->int_st.csi_async_fifo_ovf_int_st,
         (unsigned int)brg->int_st.dma_cfg_has_updated_int_st);

  if (host == NULL)
    {
      printf("csi_dump[%s]: host not initialized\n", tag);
      return;
    }

  printf("csi_dump[%s]: host lanes=%u phy_rx=0x%08lx (clk_hs=%u "
         "ulpsesc0=%u ulpsesc1=%u) stopstate=0x%08lx (data0=%u data1=%u "
         "clk=%u)\n",
         tag,
         (unsigned int)host->n_lanes.n_lanes,
         (unsigned long)host->phy_rx.val,
         (unsigned int)host->phy_rx.phy_rxclkactivehs,
         (unsigned int)host->phy_rx.phy_rxulpsesc_0,
         (unsigned int)host->phy_rx.phy_rxulpsesc_1,
         (unsigned long)host->phy_stopstate.val,
         (unsigned int)host->phy_stopstate.phy_stopstatedata_0,
         (unsigned int)host->phy_stopstate.phy_stopstatedata_1,
         (unsigned int)host->phy_stopstate.phy_stopstateclk);

  printf("csi_dump[%s]: host int_main=0x%08lx phy_fatal=0x%08lx "
         "pkt_fatal=0x%08lx bndry=0x%08lx seq=0x%08lx crc=0x%08lx "
         "pld_crc=0x%08lx data_id=0x%08lx ecc=0x%08lx phy=0x%08lx\n",
         tag,
         (unsigned long)host->int_st_main.val,
         (unsigned long)host->int_st_phy_fatal.val,
         (unsigned long)host->int_st_pkt_fatal.val,
         (unsigned long)host->int_st_bndry_frame_fatal.val,
         (unsigned long)host->int_st_seq_frame_fatal.val,
         (unsigned long)host->int_st_crc_frame_fatal.val,
         (unsigned long)host->int_st_pld_crc_fatal.val,
         (unsigned long)host->int_st_data_id.val,
         (unsigned long)host->int_st_ecc_corrected.val,
         (unsigned long)host->int_st_phy.val);

  if (dev == NULL)
    {
      printf("csi_dump[%s]: dma not initialized\n", tag);
      return;
    }

  /* ch0 enable bit is chen0.bit0 (ch1_en field name, bitpos [0]); the
   * channel is enabled by dw_gdma_ll_channel_enable writing 0x101<<ch. */
  printf("csi_dump[%s]: dma ch%u sar=0x%08lx dar=0x%08lx block_ts=0x%08lx "
         "status0=0x%08lx (cmpltd=%lu) intr=0x%08lx chen0=0x%08lx "
         "ch0_en=%u\n",
         tag,
         (unsigned int)ch,
         (unsigned long)dev->ch[ch].sar0.val,
         (unsigned long)dev->ch[ch].dar0.val,
         (unsigned long)dev->ch[ch].block_ts0.val,
         (unsigned long)dev->ch[ch].status0.val,
         (unsigned long)dw_gdma_ll_channel_get_trans_amount(dev, ch),
         (unsigned long)dw_gdma_ll_channel_get_intr_status(dev, ch),
         (unsigned long)dev->chen0.val,
         (unsigned int)((dev->chen0.val >> 0) & 0x1u));
}

/* L1.8.25f R4-B: abort-pre read-only snapshot (design ref:
 * l1825f-r4b-irq-enable-audit-20260815.md). All reads are volatile/MMIO
 * only; no writes, no IRQ change, no ESP_INTR_ENABLE, no CLIC write. */

static inline uint32_t r4b_read_clic_ie(int cpuint)
{
  /* CLIC per-interrupt control reg: DR_REG_CLIC_CTRL_BASE + (cpuint+16)*4,
   * IE bit = BIT(8) (soc/clic_reg.h). The +16 offset maps the external
   * interrupt number (0..31, as returned by esp_setup_irq) onto the CLIC
   * register space - matches rv_utils_intr_get_enabled_mask() and
   * esprv_int_set_vectored() which both use CLIC_EXT_INTR_NUM_OFFSET=16.
   * Read-only. */
  if (cpuint < 0)
    {
      return 0;
    }

  return (*(volatile uint32_t *)(0x20801000u + (uint32_t)(cpuint + 16) * 4u)) &
         (1u << 8);
}

static inline uint32_t r4b_read_mstatus_mie(void)
{
  uint32_t mstatus = 0;
  __asm__ volatile ("csrr %0, mstatus" : "=r"(mstatus));
  return (mstatus >> 3) & 1u;   /* MSTATUS_MIE = bit 3 */
}

/* L1.8.25f R4-C: read-only abort-pre snapshot augmentation. All reads are
 * volatile/MMIO only; no writes, no IRQ change, no CLIC write.
 *
 * Three new discriminating fields (see l1825f-r4c-demux-vector-audit):
 *  A) interrupt-matrix route value for DW_GDMA source 24:
 *     CORE0_GDMA_INT_MAP_REG = DR_REG_INTERRUPT_CORE0_BASE + 4*24
 *     = (DR_REG_HPPERIPH1_BASE 0x500C0000 + 0x16000 = 0x500D6000) + 0x60
 *     = 0x500D6060, field [5:0]. Expect 2 (=cpuint) if the route re-enable
 *     restored it; 6 (=INT_MUX_DISABLED_INTNO) if the INTRDISABLED disable
 *     route was never re-connected; 0 if never routed.
 *  B) CLIC pending (IP) bit of the external interrupt this source maps to:
 *     CLIC_INT_CTRL_REG(18) = 0x20801000 + 18*4 = 0x20801048, bit 0.
 *     Expect 1 if the GDMA event reached the CLIC input.
 *  C) NuttX reverse lookup: esp_cpuint_to_irq(cpuint, cpu) should return
 *     ESP_SOURCE2IRQ(24)=41 if esp_setup_irq registered the handle;
 *     esp_get_handle(cpu, irq) non-NULL proves the g_handle_map entry. */

static inline uint32_t r4c_read_intmtx_gdma_route(void)
{
  /* DR_REG_INTERRUPT_CORE0_BASE = DR_REG_INTR_BASE = 0x500D6000 */
  return (*(volatile uint32_t *)(0x500D6000u + 4u * 24u)) & 0x3fu;
}

static inline uint32_t r4c_read_clic_ip(int cpuint)
{
  if (cpuint < 0)
    {
      return 0;
    }

  /* CLIC_INT_CTRL_REG(cpuint + 16), IP bit = BIT(0). */
  return (*(volatile uint32_t *)(0x20801000u + (uint32_t)(cpuint + 16) * 4u)) &
         (1u << 0);
}

static void r4b_snapshot_abort_pre(void)
{
  dw_gdma_dev_t *dev = g_dma_dev;
  uint8_t ch = CSI_DMA_CHANNEL;
  uint32_t intr_st0    = dw_gdma_ll_channel_get_intr_status(dev, ch);
  uint32_t intr_sig    = dev->ch[ch].int_sig_ena0.val;
  uint32_t intr_st_ena = dev->ch[ch].int_st_ena0.val;
  uint32_t chen        = dev->chen0.val;
  uint32_t status0     = dev->ch[ch].status0.val;
  uint32_t clic_ie     = r4b_read_clic_ie(g_csi_block_cpuint);
  uint32_t mie         = r4b_read_mstatus_mie();

  /* R4-C: matrix route / CLIC pending / NuttX reverse lookup */
  uint32_t intmtx_route = r4c_read_intmtx_gdma_route();
  uint32_t clic_ip      = r4c_read_clic_ip(g_csi_block_cpuint);
  int      rev_irq      = esp_cpuint_to_irq(g_csi_block_cpuint, this_cpu());
  intr_handle_t h41     = esp_get_handle(this_cpu(), ESP_SOURCE2IRQ(DW_GDMA_INTR_SOURCE));

  printf("csi: abortpre intr_st0=0x%08lx sig_ena=0x%08lx st_ena=0x%08lx "
         "chen0=0x%08lx status0=0x%08lx clic_ie(cpuint=%d)=%lu mie=%lu "
         "isr_entry=%lu\n",
         (unsigned long)intr_st0,
         (unsigned long)intr_sig,
         (unsigned long)intr_st_ena,
         (unsigned long)chen,
         (unsigned long)status0,
         g_csi_block_cpuint,
         (unsigned long)clic_ie,
         (unsigned long)mie,
         (unsigned long)g_csi_isr_entry_count);

  printf("csi: r4c intmtx_gdma_route=0x%02lx (expect 0x02) "
         "clic_ip(cpuint=%d)=%lu (expect 1) "
         "rev_irq=%d (expect %d) handle41=%s\n",
         (unsigned long)intmtx_route,
         g_csi_block_cpuint,
         (unsigned long)clic_ip,
         rev_irq,
         (int)ESP_SOURCE2IRQ(DW_GDMA_INTR_SOURCE),
         h41 != NULL ? "yes" : "no");
}

/****************************************************************************
 * Name: esp32p4_csi_capture_one_rgb565
 *
 * Description:
 *   L1.8.25e Stage B (ISP path): capture one RGB565 frame into a PSRAM
 *   buffer. Frame size is fixed to width*height*2 B (RGB565) = 1,228,800 B
 *   for 1024x600; the 64-bit GDMA item count is derived via the existing
 *   camera_diag_gdma_frame_items_calc() (153,600 items). Polls trans_amount
 *   to the RGB565 item count (no interrupt callback in this first version).
 *   RAW8 path (esp32p4_csi_capture_one) is unchanged.
 *
 * Input Parameters:
 *   buffer        - Destination buffer (RGB565, PSRAM, 8 B aligned).
 *   buffer_length - Buffer size in bytes (must be >= frame_bytes).
 *   timestamp_ms  - Optional: capture timestamp in ms.
 *
 * Returned Value:
 *   Frame length in bytes on success; negative errno on failure.
 ****************************************************************************/

int esp32p4_csi_capture_one_rgb565(FAR uint8_t *buffer,
                                   size_t buffer_length,
                                   FAR uint32_t *timestamp_ms)
{
  dw_gdma_dev_t *dev = g_dma_dev;
  uint8_t ch = CSI_DMA_CHANNEL;
  uint32_t frame_items;
  uint32_t frame_bytes;
  uint32_t blocks_done = 0;
  int ret;
  int timeout;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  /* RGB565: 2 B per pixel. 1024x600 -> 1,228,800 B. */
  frame_bytes = (g_csi_cfg.frame_width * g_csi_cfg.frame_height * 16U) / 8U;

  ret = camera_diag_gdma_frame_items_calc(frame_bytes, &frame_items);
  if (ret == CSI_CALC_ERR_INVALID)
    {
      return -EINVAL;
    }

  if (ret == CSI_CALC_ERR_OVERFLOW)
    {
      return -EOVERFLOW;
    }

  if (ret != CSI_CALC_OK)
    {
      return ret;
    }

  if (buffer == NULL || buffer_length < frame_bytes)
    {
      return -EINVAL;
    }

  /* L1.8.25f R3-P2: program one contiguous block transfer (bridge FIFO ->
   * buffer). is_last=false keeps the channel enabled after each block-done
   * so the ISR can re-arm and the task loop can aggregate blocks into one
   * full RGB565 frame. */
  dw_gdma_ll_channel_set_src_addr(dev, ch, MIPI_CSI_BRG_MEM_BASE);
  dw_gdma_ll_channel_set_dst_addr(dev, ch, (uint32_t)(uintptr_t)buffer);
  dw_gdma_ll_channel_set_dst_master_port(dev, ch, (intptr_t)buffer);
  dw_gdma_ll_channel_set_trans_block_size(dev, ch, frame_items);
  dw_gdma_ll_channel_set_block_markers(dev, ch, true, false, true);

  /* Arm the ISR re-arm parameters (task context) and reset the counters.
   * L1.8.25f R4-D: frame completion is judged on the CUMULATIVE completed
   * item count (g_csi_frame_items_done) reaching frame_items (153600),
   * not on the raw block-done event count (block events fire per 960-item
   * bridge FIFO watermark, which is NOT a whole frame). */
  g_csi_isr_items = frame_items;
  g_csi_isr_dst   = (uint32_t)(uintptr_t)buffer;
  g_csi_block_count = 0;
  g_csi_frame_items_done = 0;
  g_csi_block_armed = true;

  dw_gdma_ll_channel_clear_intr(dev, ch, 0xffffffffu);

  /* Invalidate the destination buffer before DMA writes (L1.7 cache policy),
   * full RGB565 frame size. */
  esp_cache_msync(buffer, frame_bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

  /* L1.8.25e (ISP path): the CSI bridge is enabled ONCE by the caller
   * (esp32p4_csi_bridge_enable) before the frame loop and disabled once
   * after it. Here only the DMA channel is enabled per frame; bridge stays
   * on to keep the ISP data path alive (official start/stop lifecycle). */
  dw_gdma_ll_channel_enable(dev, ch, true);

  /* L1.8.25f R3-P2/R4-D: wait for a full RGB565 frame = frame_items
   * (153600) COMPLETED ITEMS accumulated in the ISR. Block-done events
   * fire at bridge FIFO watermarks (960 items), so a single block event
   * does NOT mean a whole frame; polling the cumulative item counter is
   * safe because it is a volatile counter, not the saturating cmpltd
   * read-back. */
  timeout = 500000;
  while (timeout-- > 0)
    {
      blocks_done = g_csi_block_count;
      if (g_csi_frame_items_done >= frame_items)
        {
          break;
        }

      up_udelay(10);
    }

  g_csi_block_armed = false;

  /* L1.8.25f R4-B: abort-pre read-only snapshot - BEFORE enable(false)/
   * abort(), to capture the raw pre-teardown state (int_st0 / sig_ena /
   * st_ena / chen0 / status0 / CLIC IE / mstatus.MIE / isr_entry). */
  if (g_csi_runtime_diag_enabled)
    {
      r4b_snapshot_abort_pre();
    }

  /* Timeout/cleanup path. Bridge is left enabled (caller disables once
   * after the frame loop). */
  dw_gdma_ll_channel_enable(dev, ch, false);
  dw_gdma_ll_channel_abort(dev, ch);

  if (g_csi_frame_items_done < frame_items)
    {
      /* L1.8.25f R3-P3/R4-D (read-only diag): distinguish "IRQ never
       * routed" (isr_entry==0) from "block-done is a teardown artifact"
       * (isr_entry>0 but no block event) and report how many of the
       * frame_items actually completed (cumulative items). */
      uint32_t cur_intr = dw_gdma_ll_channel_get_intr_status(dev, ch);
      printf("csi: rgb565 capture timeout (items_done=%lu need=%lu "
             "blocks=%lu isr_entry=%lu isr_last_intr=0x%08lx intr=0x%08lx "
             "chen0=0x%08lx)\n",
             (unsigned long)g_csi_frame_items_done,
             (unsigned long)frame_items,
             (unsigned long)blocks_done,
             (unsigned long)g_csi_isr_entry_count,
             (unsigned long)g_csi_isr_last_intr,
             (unsigned long)cur_intr,
             (unsigned long)dev->chen0.val);
      return -ETIMEDOUT;
    }

  esp_cache_msync(buffer, frame_bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

  if (timestamp_ms != NULL)
    {
      *timestamp_ms = (uint32_t)((clock_systime_ticks() * 1000UL) /
                                 TICK_PER_SEC);
    }

  return (int)frame_bytes;
}
int esp32p4_csi_capture_one(FAR uint8_t *buffer, size_t buffer_length,
                            FAR uint32_t *timestamp_ms)
{
  dw_gdma_dev_t *dev = g_dma_dev;
  uint8_t ch = CSI_DMA_CHANNEL;
  uint32_t frame_items;
  uint32_t frame_bytes;
  uint32_t done = 0;
  int ret;
  int timeout;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  frame_bytes = (g_csi_cfg.frame_width * g_csi_cfg.frame_height *
                 g_csi_cfg.in_bpp) / 8U;

  /* D1 (L1.8.11): DW_GDMA_TRANS_WIDTH_64 = 64 bit = 8 B per transfer item;
   * derive item count from frame bytes (never truncate, reject invalid). */
  ret = camera_diag_gdma_frame_items_calc(frame_bytes, &frame_items);
  if (ret == CSI_CALC_ERR_INVALID)
    {
      return -EINVAL;
    }

  if (ret == CSI_CALC_ERR_OVERFLOW)
    {
      return -EOVERFLOW;
    }

  if (ret != CSI_CALC_OK)
    {
      return ret;
    }

  if (buffer == NULL || buffer_length < frame_bytes)
    {
      return -EINVAL;
    }

  /* Program one contiguous block transfer: CSI bridge FIFO -> buffer */
  dw_gdma_ll_channel_set_src_addr(dev, ch, MIPI_CSI_BRG_MEM_BASE);
  dw_gdma_ll_channel_set_dst_addr(dev, ch, (uint32_t)(uintptr_t)buffer);
  dw_gdma_ll_channel_set_dst_master_port(dev, ch, (intptr_t)buffer);
  dw_gdma_ll_channel_set_trans_block_size(dev, ch, frame_items);
  dw_gdma_ll_channel_set_block_markers(dev, ch, true, true, true);
  dw_gdma_ll_channel_clear_intr(dev, ch, 0xffffffffu);

  /* Invalidate the destination buffer before DMA writes (L1.7 cache policy) */
  esp_cache_msync(buffer, frame_bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

  /* D2 (L1.8.11): enable GDMA channel FIRST, then CSI bridge
   * (ESP-IDF esp_cam_ctlr_csi.c order). Matches csi_plan_start. */
  dw_gdma_ll_channel_enable(dev, ch, true);
  mipi_csi_brg_ll_enable(g_csi_hal.bridge_dev, true);

  timeout = 500000;
  while (timeout-- > 0)
    {
      done = dw_gdma_ll_channel_get_trans_amount(dev, ch);
      if (done >= frame_items)
        {
          break;
        }

      up_udelay(10);
    }

  /* Timeout/cleanup path */
  dw_gdma_ll_channel_enable(dev, ch, false);
  dw_gdma_ll_channel_abort(dev, ch);
  mipi_csi_brg_ll_enable(g_csi_hal.bridge_dev, false);

  if (done < frame_items)
    {
      i2cerr("csi: capture timeout (transferred %lu of %lu items)\n",
             (unsigned long)done, (unsigned long)frame_items);
      return -ETIMEDOUT;
    }

  /* Invalidate again so the CPU reads DMA-written data (L1.7 cache policy) */
  esp_cache_msync(buffer, frame_bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

  if (timestamp_ms != NULL)
    {
      *timestamp_ms = (uint32_t)((clock_systime_ticks() * 1000UL) /
                                 TICK_PER_SEC);
    }

  return (int)frame_bytes;
}

/* L1.8.7: read-only CSI/GDMA register dump for first-frame-timeout diagnosis.
 * Prints CSI bridge, CSI host (incl. error interrupt status) and DW-GDMA
 * channel state. No enable/start/DMA operation is performed. */
int esp32p4_csi_diag(void)
{
  csi_brg_dev_t *brg = g_csi_hal.bridge_dev;
  csi_host_dev_t *host = g_csi_hal.host_dev;
  dw_gdma_dev_t *dev = g_dma_dev;
  uint8_t ch = CSI_DMA_CHANNEL;

  if (brg == NULL || host == NULL)
    {
      return -EINVAL;
    }

  printf("csi_diag: bridge en=%u clk=%u frame_cfg=0x%08lx dt=0x%08lx "
         "burst_len=%lu req_int=%lu bfc=0x%08lx endian=0x%08lx cm=0x%08lx\n",
         (unsigned int)brg->csi_en.csi_brg_en,
         (unsigned int)brg->host_ctrl.csi_enableclk,
         (unsigned long)brg->frame_cfg.val,
         (unsigned long)brg->data_type_cfg.val,
         (unsigned long)brg->dma_req_cfg.dma_burst_len,
         (unsigned long)brg->dma_req_interval.dma_req_interval,
         (unsigned long)brg->buf_flow_ctl.val,
         (unsigned long)brg->endian_mode.val,
         (unsigned long)brg->host_cm_ctrl.val);

  printf("csi_diag: host lanes=%u int_main=0x%08lx "
         "phy_fatal=0x%08lx pkt_fatal=0x%08lx bndry=0x%08lx seq=0x%08lx "
         "crc=0x%08lx pld_crc=0x%08lx data_id=0x%08lx ecc=0x%08lx phy=0x%08lx "
         "phy_rx=0x%08lx\n",
         (unsigned int)host->n_lanes.n_lanes,
         (unsigned long)host->int_st_main.val,
         (unsigned long)host->int_st_phy_fatal.val,
         (unsigned long)host->int_st_pkt_fatal.val,
         (unsigned long)host->int_st_bndry_frame_fatal.val,
         (unsigned long)host->int_st_seq_frame_fatal.val,
         (unsigned long)host->int_st_crc_frame_fatal.val,
         (unsigned long)host->int_st_pld_crc_fatal.val,
         (unsigned long)host->int_st_data_id.val,
         (unsigned long)host->int_st_ecc_corrected.val,
         (unsigned long)host->int_st_phy.val,
         (unsigned long)host->phy_rx.val);

  if (dev == NULL)
    {
      printf("csi_diag: dma not initialized (no GDMA in this mode)\n");
      return OK;
    }

  printf("csi_diag: dma ch%u sar=0x%08lx dar=0x%08lx block_ts=0x%08lx "
         "ctl0=0x%08lx ctl1=0x%08lx cfg0=0x%08lx cfg1=0x%08lx "
         "status0=0x%08lx intr=0x%08lx trans=%lu\n",
         (unsigned int)ch,
         (unsigned long)dev->ch[ch].sar0.val,
         (unsigned long)dev->ch[ch].dar0.val,
         (unsigned long)dev->ch[ch].block_ts0.val,
         (unsigned long)dev->ch[ch].ctl0.val,
         (unsigned long)dev->ch[ch].ctl1.val,
         (unsigned long)dev->ch[ch].cfg0.val,
         (unsigned long)dev->ch[ch].cfg1.val,
         (unsigned long)dev->ch[ch].status0.val,
         (unsigned long)dw_gdma_ll_channel_get_intr_status(dev, ch),
         (unsigned long)dw_gdma_ll_channel_get_trans_amount(dev, ch));

  return OK;
}


/* L1.8.17: read-only CSI data-plane diagnosis.
 * Enables the CSI bridge only (NO GDMA, NO frame capture), lets MIPI data
 * flow into the bridge FIFO for a fixed window, then dumps bridge FIFO
 * depth / bridge interrupt status (vadr gt/lt, discard, overrun, fifo
 * overflow, dma cfg updated), data-lane stop-state and CSI host error
 * interrupts. Disables the bridge before returning. Sensor stream-on/off
 * is handled by the caller (camera_diag --csi-data-diag). */
int esp32p4_csi_data_diag(void)
{
  csi_brg_dev_t *brg = g_csi_hal.bridge_dev;
  csi_host_dev_t *host = g_csi_hal.host_dev;

  if (brg == NULL || host == NULL)
    {
      return -EINVAL;
    }

  /* Enable bridge only; data may flow into the bridge FIFO, but no DMA
   * drains it. FIFO depth / overrun / discard evidence tells whether MIPI
   * data actually reaches the bridge (H1 data-plane check). */
  mipi_csi_brg_ll_enable(brg, true);

  /* Fixed observation window (matches runtime-diag) */
  up_udelay(500000);

  printf("csi_data: bridge en=%u clk=%u frame_cfg=0x%08lx dt=0x%08lx "
         "dma_req=0x%08lx flow_ctrl=%u burst_len=%lu dmablk=%lu req_int=%lu "
         "buf_flow=0x%08lx afull_thrd=%lu depth=%lu\n",
         (unsigned int)brg->csi_en.csi_brg_en,
         (unsigned int)brg->host_ctrl.csi_enableclk,
         (unsigned long)brg->frame_cfg.val,
         (unsigned long)brg->data_type_cfg.val,
         (unsigned long)brg->dma_req_cfg.val,
         (unsigned int)brg->dma_req_cfg.csi_dma_flow_controller,
         (unsigned long)brg->dma_req_cfg.dma_burst_len,
         (unsigned long)brg->dmablk_size.dmablk_size,
         (unsigned long)brg->dma_req_interval.dma_req_interval,
         (unsigned long)brg->buf_flow_ctl.val,
         (unsigned long)brg->buf_flow_ctl.csi_buf_afull_thrd,
         (unsigned long)brg->buf_flow_ctl.csi_buf_depth);

  printf("csi_data: bridge int_st=0x%08lx int_raw=0x%08lx "
         "(vadr_gt=%u vadr_lt=%u discard=%u overrun=%u fifo_ovf=%u dma_upd=%u)\n",
         (unsigned long)brg->int_st.val,
         (unsigned long)brg->int_raw.val,
         (unsigned int)brg->int_st.vadr_num_gt_int_st,
         (unsigned int)brg->int_st.vadr_num_lt_int_st,
         (unsigned int)brg->int_st.discard_int_st,
         (unsigned int)brg->int_st.csi_buf_overrun_int_st,
         (unsigned int)brg->int_st.csi_async_fifo_ovf_int_st,
         (unsigned int)brg->int_st.dma_cfg_has_updated_int_st);

  printf("csi_data: host lanes=%u phy_rx=0x%08lx (clk_hs=%u ulpsesc0=%u ulpsesc1=%u) "
         "stopstate=0x%08lx (data0=%u data1=%u clk=%u)\n",
         (unsigned int)host->n_lanes.n_lanes,
         (unsigned long)host->phy_rx.val,
         (unsigned int)host->phy_rx.phy_rxclkactivehs,
         (unsigned int)host->phy_rx.phy_rxulpsesc_0,
         (unsigned int)host->phy_rx.phy_rxulpsesc_1,
         (unsigned long)host->phy_stopstate.val,
         (unsigned int)host->phy_stopstate.phy_stopstatedata_0,
         (unsigned int)host->phy_stopstate.phy_stopstatedata_1,
         (unsigned int)host->phy_stopstate.phy_stopstateclk);

  printf("csi_data: host int_main=0x%08lx err phy_fatal=0x%08lx pkt_fatal=0x%08lx "
         "bndry=0x%08lx seq=0x%08lx crc=0x%08lx pld_crc=0x%08lx "
         "data_id=0x%08lx ecc=0x%08lx phy=0x%08lx\n",
         (unsigned long)host->int_st_main.val,
         (unsigned long)host->int_st_phy_fatal.val,
         (unsigned long)host->int_st_pkt_fatal.val,
         (unsigned long)host->int_st_bndry_frame_fatal.val,
         (unsigned long)host->int_st_seq_frame_fatal.val,
         (unsigned long)host->int_st_crc_frame_fatal.val,
         (unsigned long)host->int_st_pld_crc_fatal.val,
         (unsigned long)host->int_st_data_id.val,
         (unsigned long)host->int_st_ecc_corrected.val,
         (unsigned long)host->int_st_phy.val);

  mipi_csi_brg_ll_enable(brg, false);
  return OK;
}
