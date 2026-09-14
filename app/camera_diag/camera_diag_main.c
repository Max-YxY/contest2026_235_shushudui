/****************************************************************************
 * apps/examples/camera_diag/camera_diag_main.c
 *
 * S11-L1.6 camera build integration: minimal UART frame-metadata diagnostic.
 * Flow: I2C init -> SC2336 probe/init/stream -> CSI+DW-GDMA config ->
 * capture one frame -> print length/status/timestamp/SHA-256.
 *
 * Build-level integration only; no hardware operation performed. On-target
 * capture requires a separate limited hardware authorization.
 *
 * Controlled camera diagnostic; not real label inspection.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/clock.h>
#include <nuttx/arch.h>
#include <nuttx/signal.h>

#include "esp32p4_i2c.h"
#include "esp32p4_csi.h"
#include "esp32p4_csi_isp.h"
#include "esp32p4_dsi.h"
#include "sc2336.h"
#include "sha256.h"
#include "camera_diag_model_storage.h"

/* M15 loader seam: retain a link-time reference without invoking inference. */
extern int m15_espdl_loader_probe(void);
static int (*const g_m15_espdl_link_anchor)(void) __attribute__((used)) =
  m15_espdl_loader_probe;
#include "camera_diag_espdl_storage.h"
#include "camera_diag_tflm_init.h"
#include "m13_validation_samples.h"
#include "camera_diag_psram_addr.h"
#include "esp_cache.h"
#include "esp_psram.h"
#include "esp_private/esp_psram_extram.h"
#include "hal/i2c_ll.h"
#include "hal/i2c_hal.h"

#define CAMERA_DIAG_FRAME_BYTES \
  (CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH * \
   CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT)

/* L1.8: explicit PSRAM frame buffer - static .ext_ram.bss section linked into
 * external RAM (0x48000000), reachable by DW-GDMA. No heap allocation, so the
 * internal-SRAM heap shortfall (921600 B) is avoided; a link-time failure
 * surfaces at build time. */
static uint8_t g_camera_frame[CAMERA_DIAG_FRAME_BYTES]
  __attribute__((section(".ext_ram.bss")));

/* L1.8.25e Stage B: RGB565 (2 B/pixel) frame buffer for the ISP path
 * (RAW8 -> CSI -> ISP demosaic -> RGB565). Linked into PSRAM like the RAW8
 * buffer. 1024x600x2 = 1228800 B. */
#define CAMERA_DIAG_FRAME_BYTES_RGB565 \
  (CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH * \
   CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT * 2)

static uint8_t g_camera_frame_rgb565[CAMERA_DIAG_FRAME_BYTES_RGB565]
  __attribute__((section(".ext_ram.bss")));

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_usage(FILE *stream)
{
  fprintf(stream,
          "camera_diag -- controlled camera diagnostic; not real label inspection\n"
          "usage:\n"
          "  camera_diag --help    show this help\n"
          "  camera_diag --probe   I2C0 + SC2336 PID read only; no CSI/GDMA/frame buffer\n"
          "  camera_diag --i2c-diag I2C timing/bus diagnostic: clock params, SCCB\n"
          "                         attempts with interrupt status (no CSI/GDMA/frame)\n"
          "  camera_diag --psram-check PSRAM self-test only (init result, size, frame\n"
          "                         buffer address/region, 64 B head+tail write/read/restore\n"
          "                         with cache sync; no I2C/SC2336/CSI/GDMA/capture)\n"
          "  camera_diag --csi-diag read-only CSI bridge/host + DW-GDMA register dump\n"
          "  camera_diag --csi-runtime-diag stream SC2336, init CSI bridge/host (no GDMA),\n"
"  camera_diag --isp-frame  stream SC2336, init CSI + ISP (RAW8->RGB565),\n"
          "                         dump phy_rx/host-intr/bridge after a short window, stream off\n"
          "  camera_diag --isp-dsi-preview capture one ISP RGB565 frame and display it\n"
          "                         for 5 seconds; no storage or analysis\n"
          "  camera_diag --isp-dsi-live limited 10-frame ISP-to-DSI live preview\n"
          "                         (shared PSRAM buffer; auto-stops; no storage)\n"
          "  camera_diag --isp-dsi-stability run the same preview for 60 seconds\n"
          "                         with one-second heartbeats; auto-stops; no storage\n"
          "                         (no I2C/SC2336/enable/start; for first-frame-timeout diagnosis)\n"
          "  camera_diag --csi-data-diag stream SC2336, init CSI bridge/host (no GDMA,\n"
          "                         no capture), enable bridge FIFO, dump FIFO depth /\n"
          "                         data-lane stop-state / bridge+host error state, stream off\n"
          "  camera_diag --dsi-pattern [mode] EK79007 MIPI-DSI panel solid-color / color bar\n"
          "                         (mode 0=red 1=green 2=8-bar; no camera, no capture)\n"
          "  camera_diag --dsi-ui  minimal OpenVela-compatible status UI for 5 seconds\n"
          "                         (no camera, no capture, no stored image)\n"
          "  camera_diag --dsi-ui-live start the persistent status UI and return to NSH\n"
          "                         (no camera, no capture, no stored image)\n"
          "  camera_diag --touch-diag probe the GT911 touch controller and print touch\n"
          "                         coordinates for 10 seconds (no camera or display changes)\n"
          "  camera_diag --touch-ui start the status UI if needed, then show GT911 touch\n"
          "                         markers, coordinates, and drag trails for 5 minutes\n"
          "  camera_diag --touch-ui-live start the status UI and keep GT911 touch\n"
          "                         feedback active until the system restarts\n"
          "  camera_diag --model-storage map and validate the external Flash model\n"
          "                         container; no TFLM, camera, DSI, or touch activity\n"
          "  camera_diag --espdl-storage validate the proposed ESP-DL v2 container\n"
          "                         read-only; no inference, camera, DSI, or touch activity\n"
          "  camera_diag --tflm-init initialize TFLite Micro tensors from the validated\n"
          "                         external model; no inference, camera, DSI, or touch\n"
          "  camera_diag --tflm-nms-selftest verify output NMS with synthetic int8 data;\n"
          "                         no model, camera, DSI, touch, or Flash activity\n"
          "  camera_diag --result-ui-selftest show the controlled two-candidate result\n"
          "                         card; no camera, model, or Flash activity\n"
          "  camera_diag --energy-ui-selftest show defect and energy-level result\n"
          "                         card; no camera, model, or Flash activity\n"
          "  camera_diag           run the full capture chain once (requires separate\n"
          "                         hardware authorization)\n");
}

#define GT911_ADDR_PRIMARY  0x5d
#define GT911_ADDR_SECONDARY 0x14
#define GT911_PRODUCT_ID_REG 0x8140
#define GT911_PRODUCT_ID_LEN 3
#define GT911_STATUS_REG 0x814e
#define GT911_POINT1_REG 0x814f
#define GT911_CAL_X_MIN 9
#define GT911_CAL_X_MAX 998
#define GT911_CAL_Y_MIN 30
#define GT911_CAL_Y_MAX 569

static uint16_t gt911_map_axis(uint16_t value, uint16_t raw_min,
                               uint16_t raw_max, uint16_t output_max)
{
  if (value <= raw_min)
    {
      return 0;
    }

  if (value >= raw_max)
    {
      return output_max;
    }

  return (uint16_t)(((unsigned int)(value - raw_min) * output_max) /
                     (raw_max - raw_min));
}

static int gt911_read(FAR struct i2c_master_s *i2c, uint8_t address,
                      uint16_t register_address, FAR uint8_t *buffer,
                      size_t length)
{
  size_t offset;

  for (offset = 0; offset < length; offset++)
    {
      uint16_t current_register = register_address + offset;
      uint8_t register_bytes[2] =
      {
        (uint8_t)(current_register >> 8),
        (uint8_t)current_register,
      };
      struct i2c_msg_s messages[2] =
      {
        {
          .frequency = CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
          .addr = address,
          .flags = 0,
          .buffer = register_bytes,
          .length = sizeof(register_bytes),
        },
        {
          .frequency = CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
          .addr = address,
          .flags = I2C_M_READ,
          .buffer = &buffer[offset],
          .length = 1,
        },
      };
      int ret = I2C_TRANSFER(i2c, messages, 2);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

static int gt911_write(FAR struct i2c_master_s *i2c, uint8_t address,
                       uint16_t reg, uint8_t value)
{
  uint8_t message[3] =
  {
    (uint8_t)(reg >> 8),
    (uint8_t)reg,
    value,
  };
  struct i2c_msg_s transfer =
  {
    .frequency = CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
    .addr = address,
    .flags = 0,
    .buffer = message,
    .length = sizeof(message),
  };

  return I2C_TRANSFER(i2c, &transfer, 1);
}

/* Workflow button handler: called when a workflow stage button is pressed */
static void workflow_button_handler(int stage)
{
  printf("workflow: button pressed stage=%d\n", stage);

  switch (stage)
    {
      case 1: /* CAPTURE */
        printf("workflow: CAPTURE - capturing frame...\n");
        /* TODO: trigger camera capture */
        break;

      case 2: /* GALLERY */
        printf("workflow: GALLERY - opening gallery...\n");
        /* TODO: show gallery view */
        break;

      case 3: /* LABEL */
        printf("workflow: LABEL - running label detection...\n");
        /* TODO: run label detection */
        break;

      case 4: /* ENERGY */
        printf("workflow: ENERGY - showing energy level...\n");
        /* TODO: show energy level details */
        break;

      case 5: /* STAIN */
        printf("workflow: STAIN - running stain detection...\n");
        /* TODO: run stain defect detection */
        break;

      case 6: /* DAMAGE */
        printf("workflow: DAMAGE - running damage detection...\n");
        /* TODO: run damage defect detection */
        break;

      case 7: /* WRINKLE */
        printf("workflow: WRINKLE - running wrinkle detection...\n");
        /* TODO: run wrinkle defect detection */
        break;

      case 8: /* POSITION */
        printf("workflow: POSITION - checking position...\n");
        /* TODO: run position deviation detection */
        break;

      default:
        printf("workflow: unknown stage %d\n", stage);
        break;
    }
}

static int touch_diag(int duration_ms, bool display_feedback)
{
  FAR struct i2c_master_s *i2c;
  uint8_t point[6];
  uint8_t status;
  uint8_t last_status = 0xff;
  char product_id[GT911_PRODUCT_ID_LEN + 1];
  uint8_t address = GT911_ADDR_PRIMARY;
  bool found = false;
  unsigned int ready_events = 0;
  bool feedback_started = false;
  uint16_t feedback_x = 0;
  uint16_t feedback_y = 0;
  int ret;
  int elapsed_ms;

  i2c = esp32p4_i2c_bus_initialize(CONFIG_EXAMPLES_CAMERA_DIAG_I2C_PORT,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SCL_PIN,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SDA_PIN);
  if (i2c == NULL)
    {
      fprintf(stderr, "touch_diag: I2C0 initialization failed\n");
      return 1;
    }

  ret = gt911_read(i2c, GT911_ADDR_PRIMARY, GT911_PRODUCT_ID_REG,
                   (FAR uint8_t *)product_id, GT911_PRODUCT_ID_LEN);
  product_id[GT911_PRODUCT_ID_LEN] = '\0';
  if (ret == OK && product_id[0] == '9')
    {
      found = true;
      address = GT911_ADDR_PRIMARY;
    }
  else
    {
      ret = gt911_read(i2c, GT911_ADDR_SECONDARY, GT911_PRODUCT_ID_REG,
                       (FAR uint8_t *)product_id, GT911_PRODUCT_ID_LEN);
      product_id[GT911_PRODUCT_ID_LEN] = '\0';
      if (ret == OK && product_id[0] == '9')
        {
          found = true;
          address = GT911_ADDR_SECONDARY;
        }
    }

  if (!found)
    {
      fprintf(stderr, "touch_diag: GT911 not detected at 0x%02x or 0x%02x\n",
              GT911_ADDR_PRIMARY, GT911_ADDR_SECONDARY);
      return 1;
    }

  printf("touch_diag: GT911 product=%s address=0x%02x I2C0 GPIO8/7\n",
         product_id, address);
  if (duration_ms < 0)
    {
      printf("touch_diag: poll continuously; touch the panel to report coordinates%s\n",
             display_feedback ? " and show display feedback" : "");
    }
  else
    {
      printf("touch_diag: poll for %u seconds; touch the panel to report coordinates%s\n",
             (unsigned int)(duration_ms / 1000),
             display_feedback ? " and show display feedback" : "");
    }

  for (elapsed_ms = 0; duration_ms < 0 || elapsed_ms < duration_ms; )
    {
      ret = gt911_read(i2c, address, GT911_STATUS_REG, &status,
                       sizeof(status));
      if (ret < 0)
        {
          fprintf(stderr, "touch_diag: status read failed: %d\n", ret);
          return 1;
        }

      if (status != last_status)
        {
          printf("touch_diag: status=0x%02x\n", status);
          last_status = status;
        }

      if ((status & 0x80) != 0)
        {
          ready_events++;
          if ((status & 0x0f) != 0)
            {
              ret = gt911_read(i2c, address, GT911_POINT1_REG, point,
                               sizeof(point));
              if (ret < 0)
                {
                  fprintf(stderr, "touch_diag: point read failed: %d\n", ret);
                  return 1;
                }

              uint16_t x = (unsigned int)point[1] |
                           ((unsigned int)point[2] << 8);
              uint16_t y = (unsigned int)point[3] |
                           ((unsigned int)point[4] << 8);
              uint16_t display_x = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH - 1 -
                gt911_map_axis(x, GT911_CAL_X_MIN, GT911_CAL_X_MAX,
                               CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH - 1);
              uint16_t display_y =
                CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT - 1 -
                gt911_map_axis(y, GT911_CAL_Y_MIN, GT911_CAL_Y_MAX,
                               CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT - 1);

              printf("touch_diag: point id=%u x=%u y=%u display_x=%u display_y=%u\n",
                     point[0] & 0x0f, (unsigned int)x, (unsigned int)y,
                     (unsigned int)display_x, (unsigned int)display_y);
              if (display_feedback &&
                  (!feedback_started || display_x != feedback_x ||
                   display_y != feedback_y))
                {
                  ret = esp32p4_dsi_status_ui_touch_feedback(
                    display_x, display_y, ready_events, !feedback_started);
                  if (ret < 0)
                    {
                      fprintf(stderr,
                              "touch_diag: display feedback failed: %d\n", ret);
                      return 1;
                    }

                  feedback_started = true;
                  feedback_x = display_x;
                  feedback_y = display_y;
                }
            }
          else if (display_feedback && feedback_started)
            {
              esp32p4_dsi_status_ui_touch_release();
              feedback_started = false;
            }

          ret = gt911_write(i2c, address, GT911_STATUS_REG, 0);
          if (ret < 0)
            {
              fprintf(stderr, "touch_diag: status clear failed: %d\n", ret);
              return 1;
            }
        }

      nxsig_usleep(50 * 1000);

      if (duration_ms >= 0)
        {
          elapsed_ms += 50;
        }
    }

  printf("touch_diag: completed ready_events=%u\n", ready_events);
  return 0;
}

/* L1.8.1 PSRAM self-check: memory only. No I2C/SC2336/CSI/GDMA/capture. */
static int psram_check(void)
{
  static const uint8_t pat[64] =
  {
    0x5a, 0xa5, 0x3c, 0xc3, 0x55, 0xaa, 0x0f, 0xf0,
    0x5a, 0xa5, 0x3c, 0xc3, 0x55, 0xaa, 0x0f, 0xf0,
    0x5a, 0xa5, 0x3c, 0xc3, 0x55, 0xaa, 0x0f, 0xf0,
    0x5a, 0xa5, 0x3c, 0xc3, 0x55, 0xaa, 0x0f, 0xf0,
    0xa5, 0x5a, 0xc3, 0x3c, 0xaa, 0x55, 0xf0, 0x0f,
    0xa5, 0x5a, 0xc3, 0x3c, 0xaa, 0x55, 0xf0, 0x0f,
    0xa5, 0x5a, 0xc3, 0x3c, 0xaa, 0x55, 0xf0, 0x0f,
    0xa5, 0x5a, 0xc3, 0x3c, 0xaa, 0x55, 0xf0, 0x0f
  };
  FAR uint8_t *frame = g_camera_frame;
  uint8_t saved[64];
  uintptr_t vstart;
  uintptr_t vend;
  uintptr_t faddr;
  esp_err_t err;
  size_t size;
  int rc = 0;
  int i;

  printf("psram_check: is_initialized=%d\n", esp_psram_is_initialized());
  size = esp_psram_get_size();
  printf("psram_check: size=%lu bytes\n", (unsigned long)size);

  vstart = esp_psram_extram_vaddr_start();
  vend = esp_psram_extram_vaddr_end();
  faddr = (uintptr_t)frame;
  printf("psram_check: extram vaddr 0x%08lx..0x%08lx\n",
         (unsigned long)vstart, (unsigned long)vend);
  printf("psram_check: frame_addr=0x%08lx\n", (unsigned long)faddr);

  /* L1.8.4: the static .ext_ram.bss frame buffer is linked at the mapping
   * base 0x48000000, which lies BEFORE esp_psram_extram_vaddr_start()
   * (the PSRAM heap start). Valid bound is therefore
   * 0x48000000 <= faddr < extram_vaddr_end (see camera_diag_psram_addr.h). */
  if (camera_diag_frame_addr_ok(faddr, vend) != 0)
    {
      fprintf(stderr, "psram_check: frame buffer NOT in external RAM region\n");
      return 1;
    }

  printf("psram_check: frame buffer in external RAM region (0x48000000+)\n");

  /* Head (first 64 B) and tail (last 64 B): save -> write pattern ->
   * cache invalidate -> read back -> verify -> restore. */
  for (i = 0; i < 2; i++)
    {
      FAR uint8_t *p = (i == 0) ? frame : frame + CAMERA_DIAG_FRAME_BYTES - 64;
      int j;
      int ok = 1;

      memcpy(saved, p, 64);
      memcpy(p, pat, 64);
      /* L1.8.5: CPU wrote the pattern, so sync cache -> memory (C2M) and
       * invalidate so the read-back reloads from memory. M2C would drop the
       * un-written-back dirty line and the read-back would see the old value
       * (observed on target 2026-08-12: head/tail mismatch at 0). */
      err = esp_cache_msync(p, 64, ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                           ESP_CACHE_MSYNC_FLAG_INVALIDATE);
      printf("psram_check: %s write+msync err=%d\n", i == 0 ? "head" : "tail",
             (int)err);
      if (err != ESP_OK)
        {
          rc = 1;
        }

      for (j = 0; j < 64; j++)
        {
          if (p[j] != pat[j])
            {
              fprintf(stderr, "psram_check: %s mismatch at %d\n",
                      i == 0 ? "head" : "tail", j);
              ok = 0;
              rc = 1;
              break;
            }
        }

      printf("psram_check: %s readback %s\n", i == 0 ? "head" : "tail",
             ok ? "OK" : "FAIL");
      memcpy(p, saved, 64);
      /* L1.8.5: same direction for the restore write-back. */
      err = esp_cache_msync(p, 64, ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                           ESP_CACHE_MSYNC_FLAG_INVALIDATE);
      if (err != ESP_OK)
        {
          rc = 1;
        }
    }

  printf("psram_check: done rc=%d\n", rc);
  return rc;
}

static void i2c_diag_report_intr(void)
{
  i2c_dev_t *hw = I2C_LL_GET_HW(CONFIG_EXAMPLES_CAMERA_DIAG_I2C_PORT);
  uint32_t raw = 0;

  i2c_ll_get_intr_raw_mask(hw, &raw);
  printf("i2c_diag: intr_raw=0x%08lx nack=%u timeout=%u end_detect=%u arb=%u\n",
         (unsigned long)raw,
         (raw & I2C_LL_INTR_NACK) != 0 ? 1 : 0,
         (raw & I2C_LL_INTR_TIMEOUT) != 0 ? 1 : 0,
         (raw & I2C_LL_INTR_END_DETECT) != 0 ? 1 : 0,
         (raw & I2C_LL_INTR_ARBITRATION) != 0 ? 1 : 0);
}

static int i2c_diag(void)
{
  FAR struct i2c_master_s *i2c;
  i2c_hal_clk_config_t clk_cal;
  uint8_t val = 0;
  int attempt;
  int ret;

  /* Safety gate (diagnostic): I2C0 only. No CSI, no GDMA, no frame buffer. */
  i2c = esp32p4_i2c_bus_initialize(CONFIG_EXAMPLES_CAMERA_DIAG_I2C_PORT,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SCL_PIN,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SDA_PIN);
  if (i2c == NULL)
    {
      fprintf(stderr, "camera_diag: I2C init failed\n");
      return 1;
    }

  printf("i2c_diag: port=%d freq=%lu scl_pin=%d sda_pin=%d\n",
         CONFIG_EXAMPLES_CAMERA_DIAG_I2C_PORT,
         (unsigned long)CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
         CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SCL_PIN,
         CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SDA_PIN);

  i2c_ll_master_cal_bus_clk(80000000U, CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
                            &clk_cal);
  printf("i2c_diag: clk clkm_div=%lu scl_low=%lu scl_high=%lu "
         "scl_wait_high=%lu sda_hold=%lu sda_sample=%lu tout=%d\n",
         (unsigned long)clk_cal.clkm_div,
         (unsigned long)clk_cal.scl_low,
         (unsigned long)clk_cal.scl_high,
         (unsigned long)clk_cal.scl_wait_high,
         (unsigned long)clk_cal.sda_hold,
         (unsigned long)clk_cal.sda_sample,
         clk_cal.tout);

  for (attempt = 1; attempt <= 3; attempt++)
    {
      struct i2c_msg_s msgs[2];
      uint8_t reg_buf[2] = { 0x31, 0x07 }; /* SC2336_REG_SENSOR_ID_H */

      i2c_diag_report_intr();

      msgs[0].frequency = CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ;
      msgs[0].addr      = SC2336_SCCB_ADDR;
      msgs[0].flags     = 0;
      msgs[0].buffer    = reg_buf;
      msgs[0].length    = 2;

      msgs[1].frequency = CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ;
      msgs[1].addr      = SC2336_SCCB_ADDR;
      msgs[1].flags     = I2C_M_READ;
      msgs[1].buffer    = &val;
      msgs[1].length    = 1;

      ret = I2C_TRANSFER(i2c, msgs, 2);
      printf("i2c_diag: attempt %d ret=%d val=0x%02x\n", attempt, ret, val);
      i2c_diag_report_intr();

      if (ret < 0)
        {
          up_udelay(10000);
          continue;
        }

      break;
    }

  printf("i2c_diag: done\n");
  return 0;
}

static int probe_pid_only(void)
{
  FAR struct i2c_master_s *i2c;
  uint16_t pid = 0;
  int ret;

  /* Safety gate (L1.6.1): I2C0 only. No CSI, no GDMA, no frame buffer. */
  i2c = esp32p4_i2c_bus_initialize(CONFIG_EXAMPLES_CAMERA_DIAG_I2C_PORT,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SCL_PIN,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SDA_PIN);
  if (i2c == NULL)
    {
      fprintf(stderr, "camera_diag: I2C init failed\n");
      return 1;
    }

  printf("camera_diag: i2c ok\n");

  ret = sc2336_probe(i2c, SC2336_SCCB_ADDR, &pid);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 probe failed: %d\n", ret);
      return 1;
    }

  printf("camera_diag: probe pid=0x%04x\n", pid);
  if (pid != SC2336_PID)
    {
      fprintf(stderr, "camera_diag: sensor PID mismatch (expected 0x%04x)\n",
              SC2336_PID);
      return 1;
    }

  printf("camera_diag: probe OK\n");
  return 0;
}

static void sha256_hex(FAR const uint8_t *buf, size_t len, FAR char *out)
{
  struct sha256_ctx_s ctx;
  uint8_t digest[32];
  int i;

  sha256_init(&ctx);
  sha256_update(&ctx, buf, len);
  sha256_final(&ctx, digest);

  for (i = 0; i < 32; i++)
    {
      sprintf(out + i * 2, "%02x", digest[i]);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static int csi_runtime_diag(void)
{
  FAR struct i2c_master_s *i2c;
  struct esp32p4_csi_config_s cfg;
  uint16_t pid = 0;
  int ret;

  /* 1. I2C bus (SC2336 SCCB control) */
  i2c = esp32p4_i2c_bus_initialize(CONFIG_EXAMPLES_CAMERA_DIAG_I2C_PORT,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SCL_PIN,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SDA_PIN);
  if (i2c == NULL)
    {
      fprintf(stderr, "camera_diag: I2C init failed\n");
      return 1;
    }

  printf("camera_diag: i2c ok\n");

  /* 2. SC2336 probe */
  ret = sc2336_probe(i2c, SC2336_SCCB_ADDR, &pid);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 probe failed: %d\n", ret);
      return 1;
    }

  if (pid != SC2336_PID)
    {
      fprintf(stderr, "camera_diag: unexpected PID 0x%04x (expect 0x%04x)\n",
              pid, SC2336_PID);
      return 1;
    }

  printf("camera_diag: sc2336 pid=0x%04x\n", pid);

  /* 3. SC2336 init + stream on */
  ret = sc2336_init(i2c, SC2336_SCCB_ADDR);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 init failed: %d\n", ret);
      return 1;
    }

  ret = sc2336_stream(i2c, SC2336_SCCB_ADDR, true);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 stream-on failed: %d\n", ret);
      return 1;
    }

  /* L1.8.16: log the actual mode from Kconfig instead of the stale
   * hard-coded text (mode itself is unchanged; sc2336_init picks the
   * register table by CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH/HEIGHT). */
  printf("camera_diag: sc2336 streaming (%ux%u RAW8 %u Mbps/lane)\n",
         CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH,
         CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT,
         CONFIG_EXAMPLES_CAMERA_DIAG_LANE_RATE_MBPS);

  /* 4. CSI bridge/host init only - no GDMA alloc/start (D1/D2 not exercised) */
  memset(&cfg, 0, sizeof(cfg));
  cfg.lanes_num        = CONFIG_EXAMPLES_CAMERA_DIAG_CSI_LANES;
  cfg.frame_width      = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH;
  cfg.frame_height     = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT;
  cfg.in_bpp           = 8;
  cfg.out_bpp          = 8;
  cfg.byte_swap_en     = false;
  cfg.lane_bit_rate_mbps = CONFIG_EXAMPLES_CAMERA_DIAG_LANE_RATE_MBPS;

  ret = esp32p4_csi_brg_init(&cfg);
  if (ret != OK)
    {
      fprintf(stderr, "camera_diag: CSI bridge init failed: %d\n", ret);
      sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
      return 1;
    }

  printf("camera_diag: csi bridge/host init ok; waiting 500ms\n");
  up_udelay(500000); /* fixed short window (~15 frames @ 30fps) */

  /* 5. Dump phy_rx / host interrupt status / bridge state */
  printf("camera_diag: csi runtime register dump\n");
  ret = esp32p4_csi_diag();

  /* 6. Stream off and exit */
  sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
  /* L1.8.25f R2: release the CSI bridge claim taken by
   * esp32p4_csi_brg_init() (no ISP in this diag). */
  esp32p4_csi_disable();
  printf("camera_diag: stream off\n");
  return ret;
}

static int isp_frame_diag(void)
{
  FAR struct i2c_master_s *i2c;
  struct esp32p4_csi_config_s cfg;
  struct esp32p4_csi_isp_config_s isp_cfg;
  FAR uint8_t *frame;
  uint16_t pid = 0;
  int ret;
  int i;
  int ok_frames = 0;
  int preprocess_rc = -1;
  const int target_frames = 3;
  uint32_t ts_ms = 0;
  bool status_ui_was_active;

  /* DSI and CSI share DW_GDMA_INTR_SOURCE and the CSI bridge claim.  A
   * persistent status UI owns both while it is active, so suspend only the
   * live UI session for this capture transaction.  The UI is restored on
   * every exit path below; the frozen touch/display baseline is unchanged. */
  status_ui_was_active = esp32p4_dsi_status_ui_is_active();
  if (status_ui_was_active)
    {
      esp32p4_dsi_status_ui_stop();
      printf("camera_diag: status UI paused for CSI/ISP capture\n");
    }

  i2c = esp32p4_i2c_bus_initialize(CONFIG_EXAMPLES_CAMERA_DIAG_I2C_PORT,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SCL_PIN,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SDA_PIN);
  if (i2c == NULL)
    {
      fprintf(stderr, "camera_diag: I2C init failed\n");
      if (status_ui_was_active)
        {
          (void)esp32p4_dsi_status_ui_start();
        }
      return 1;
    }

  printf("camera_diag: i2c ok\n");

  ret = sc2336_probe(i2c, SC2336_SCCB_ADDR, &pid);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 probe failed: %d\n", ret);
      if (status_ui_was_active)
        {
          (void)esp32p4_dsi_status_ui_start();
        }
      return 1;
    }

  if (pid != SC2336_PID)
    {
      fprintf(stderr, "camera_diag: unexpected PID 0x%04x (expect 0x%04x)\n",
              pid, SC2336_PID);
      if (status_ui_was_active)
        {
          (void)esp32p4_dsi_status_ui_start();
        }
      return 1;
    }

  printf("camera_diag: sc2336 pid=0x%04x\n", pid);

  ret = sc2336_init(i2c, SC2336_SCCB_ADDR);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 init failed: %d\n", ret);
      if (status_ui_was_active)
        {
          (void)esp32p4_dsi_status_ui_start();
        }
      return 1;
    }

  ret = sc2336_stream(i2c, SC2336_SCCB_ADDR, true);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 stream-on failed: %d\n", ret);
      if (status_ui_was_active)
        {
          (void)esp32p4_dsi_status_ui_start();
        }
      return 1;
    }

  printf("camera_diag: sc2336 streaming (%ux%u RAW8 %u Mbps/lane)\n",
         CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH,
         CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT,
         CONFIG_EXAMPLES_CAMERA_DIAG_LANE_RATE_MBPS);

  memset(&cfg, 0, sizeof(cfg));
  cfg.lanes_num          = CONFIG_EXAMPLES_CAMERA_DIAG_CSI_LANES;
  cfg.frame_width        = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH;
  cfg.frame_height       = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT;
  /* L1.8.25e (ISP path): RGB565 CSI controller config (in/out bpp = 16),
   * matching the verified official path (CSI input = ISP output = RGB565). */
  cfg.in_bpp             = 16;
  cfg.out_bpp            = 16;
  cfg.byte_swap_en       = false;
  cfg.lane_bit_rate_mbps = CONFIG_EXAMPLES_CAMERA_DIAG_LANE_RATE_MBPS;

  ret = esp32p4_csi_init(&cfg);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: CSI init failed: %d\n", ret);
      sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
      if (status_ui_was_active)
        {
          (void)esp32p4_dsi_status_ui_start();
        }
      return 1;
    }

  printf("camera_diag: csi+dwgdma configured (RGB565 in/out bpp=16)\n");

  memset(&isp_cfg, 0, sizeof(isp_cfg));
  isp_cfg.frame_width  = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH;
  isp_cfg.frame_height = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT;
  isp_cfg.clk_hz       = 80000000;

  ret = esp32p4_csi_isp_init(&isp_cfg);
  if (ret != OK)
    {
      fprintf(stderr, "camera_diag: ISP init failed: %d\n", ret);
      sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
      esp32p4_csi_disable();
      if (status_ui_was_active)
        {
          (void)esp32p4_dsi_status_ui_start();
        }
      return 1;
    }

  printf("camera_diag: isp processor enabled (RAW8->RGB565)\n");

  /* L1.8.25f R3-P2: install the DW-GDMA block-done ISR for block-count
   * based frame completion (SRC flow-controller mode; trans_amount polling
   * cannot complete a frame). Only the --isp-frame path uses it. */
  ret = esp32p4_csi_install_block_isr();
  if (ret != OK)
    {
      fprintf(stderr, "camera_diag: block ISR install failed: %d\n", ret);
      sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
      esp32p4_csi_disable();
      esp32p4_csi_isp_deinit();
      if (status_ui_was_active)
        {
          (void)esp32p4_dsi_status_ui_start();
        }
      return 1;
    }

  /* L1.8.25e: switch CSI bridge data path to RGB565 (ISP output). */
  esp32p4_csi_brg_set_rgb565_out();

  frame = g_camera_frame_rgb565;
  esp32p4_csi_enable();

  /* L1.8.25e (lifecycle): enable the bridge ONCE before the frame loop and
   * disable ONCE after it (or after the first failure). Per-frame toggling
   * would cut the ISP data path (official start/stop lifecycle). */
  esp32p4_csi_bridge_enable();

  /* L1.8.25e (diagnostic): read-only dump right after start. */
  esp32p4_csi_isp_dump("START");

  for (i = 0; i < target_frames; i++)
    {
      int frame_len = esp32p4_csi_capture_one_rgb565(frame,
                                                     CAMERA_DIAG_FRAME_BYTES_RGB565,
                                                     &ts_ms);
      if (frame_len > 0)
        {
          ok_frames++;
          printf("camera_diag: FRAME %d/%d len=%d rgb565 ts_ms=%lu\n",
                 i + 1, target_frames, frame_len, (unsigned long)ts_ms);
        }
      else
        {
          printf("camera_diag: FRAME %d/%d FAILED rc=%d\n",
                 i + 1, target_frames, frame_len);
          if (i == 0)
            {
              /* L1.8.25e (diagnostic): read-only dump after the first-frame
               * timeout, before any cleanup, to see where the data path
               * stopped. */
              esp32p4_csi_isp_dump("TIMEOUT");
            }
        }
    }

  /* Feed the last real ISP frame through the already unit-tested TFLM
   * converter before releasing the capture resources.  This keeps M12's
   * acceptance tied to the same camera frame used by the three-frame test. */
  if (ok_frames > 0)
    {
      preprocess_rc = camera_diag_tflm_preprocess_diag(
        frame, CAMERA_DIAG_FRAME_BYTES_RGB565,
        CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH,
        CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT);
      printf("camera_diag: tflm preprocess rc=%d\n", preprocess_rc);
    }

  /* L1.8.25e (diagnostic): read-only dump just before cleanup. */
  esp32p4_csi_isp_dump("CLEANUP");
  esp32p4_csi_bridge_disable();
  esp32p4_csi_disable();
  esp32p4_csi_isp_deinit();
  sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
  printf("camera_diag: stream off\n");

  printf("camera_diag: ISP_SUSTAINED %d/%d frames completed\n",
         ok_frames, target_frames);
  if (status_ui_was_active)
    {
      ret = esp32p4_dsi_status_ui_start();
      printf("camera_diag: status UI restored rc=%d\n", ret);
      if (ret == OK && preprocess_rc == 0 &&
          camera_diag_tflm_last_defect_nms_count() >= 0)
        {
          ret = esp32p4_dsi_status_ui_set_inspection_result(
            camera_diag_tflm_last_defect_nms_count(),
            camera_diag_tflm_last_energy_level());
          (void)esp32p4_dsi_status_ui_set_label_detected(
            camera_diag_tflm_last_label_detected());
          printf("camera_diag: inspection result UI rc=%d defect_candidates=%d "
                 "energy_level=%d\n", ret,
                 camera_diag_tflm_last_defect_nms_count(),
                 camera_diag_tflm_last_energy_level());
        }
    }
  return ok_frames == target_frames && preprocess_rc == 0 ? 0 : 1;
}

static int isp_dsi_preview(void)
{
  FAR struct i2c_master_s *i2c;
  struct esp32p4_csi_config_s cfg;
  struct esp32p4_csi_isp_config_s isp_cfg;
  uint16_t pid = 0;
  uint32_t ts_ms = 0;
  int frame_len;
  int ret;

  i2c = esp32p4_i2c_bus_initialize(CONFIG_EXAMPLES_CAMERA_DIAG_I2C_PORT,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SCL_PIN,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SDA_PIN);
  if (i2c == NULL)
    {
      return 1;
    }

  ret = sc2336_probe(i2c, SC2336_SCCB_ADDR, &pid);
  if (ret < 0 || pid != SC2336_PID ||
      sc2336_init(i2c, SC2336_SCCB_ADDR) < 0 ||
      sc2336_stream(i2c, SC2336_SCCB_ADDR, true) < 0)
    {
      return 1;
    }

  memset(&cfg, 0, sizeof(cfg));
  cfg.lanes_num = CONFIG_EXAMPLES_CAMERA_DIAG_CSI_LANES;
  cfg.frame_width = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH;
  cfg.frame_height = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT;
  cfg.in_bpp = 16;
  cfg.out_bpp = 16;
  cfg.lane_bit_rate_mbps = CONFIG_EXAMPLES_CAMERA_DIAG_LANE_RATE_MBPS;
  ret = esp32p4_csi_init(&cfg);
  if (ret != OK)
    {
      goto stream_off;
    }

  memset(&isp_cfg, 0, sizeof(isp_cfg));
  isp_cfg.frame_width = cfg.frame_width;
  isp_cfg.frame_height = cfg.frame_height;
  isp_cfg.clk_hz = 80000000;
  isp_cfg.enable_fixed_wb = true;
  ret = esp32p4_csi_isp_init(&isp_cfg);
  if (ret != OK || esp32p4_csi_install_block_isr() != OK)
    {
      goto csi_off;
    }

  esp32p4_csi_brg_set_rgb565_out();
  esp32p4_csi_enable();
  esp32p4_csi_bridge_enable();
  frame_len = esp32p4_csi_capture_one_rgb565(
    g_camera_frame_rgb565, CAMERA_DIAG_FRAME_BYTES_RGB565, &ts_ms);
  esp32p4_csi_bridge_disable();
  esp32p4_csi_disable();
  esp32p4_csi_isp_deinit();
  sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
  if (frame_len != CAMERA_DIAG_FRAME_BYTES_RGB565)
    {
      printf("camera_diag: preview capture failed: %d\n", frame_len);
      return 1;
    }

  if (camera_diag_tflm_preprocess_diag(
        g_camera_frame_rgb565, frame_len,
        CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH,
        CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT) != 0)
    {
      printf("camera_diag: TFLM preprocessing failed\n");
      return 1;
    }

  printf("camera_diag: preview frame len=%d ts_ms=%lu\n", frame_len,
         (unsigned long)ts_ms);
  ret = esp32p4_dsi_show_rgb565(g_camera_frame_rgb565, frame_len, 5000);
  printf("camera_diag: preview done rc=%d\n", ret);
  return ret == OK ? 0 : 1;

csi_off:
  esp32p4_csi_disable();
  esp32p4_csi_isp_deinit();
stream_off:
  sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
  return 1;
}

static int isp_dsi_preview_burst(void)
{
  int frame_index;

  for (frame_index = 0; frame_index < 3; frame_index++)
    {
      printf("camera_diag: preview burst frame %d/3\n", frame_index + 1);
      if (isp_dsi_preview() != 0)
        {
          return 1;
        }
    }

  printf("camera_diag: preview burst 3/3 completed\n");
  return 0;
}

static int isp_dsi_live(uint32_t duration_ms)
{
  FAR struct i2c_master_s *i2c;
  struct esp32p4_csi_config_s cfg;
  struct esp32p4_csi_isp_config_s isp_cfg;
  const int target_frames = 10;
  uint32_t start_ms;
  uint32_t last_report_ms;
  uint16_t pid = 0;
  uint32_t ts_ms = 0;
  int frame_index;
  int frame_len;
  int ret;

  i2c = esp32p4_i2c_bus_initialize(CONFIG_EXAMPLES_CAMERA_DIAG_I2C_PORT,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SCL_PIN,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SDA_PIN);
  if (i2c == NULL)
    {
      return 1;
    }

  ret = sc2336_probe(i2c, SC2336_SCCB_ADDR, &pid);
  if (ret < 0 || pid != SC2336_PID ||
      sc2336_init(i2c, SC2336_SCCB_ADDR) < 0 ||
      sc2336_stream(i2c, SC2336_SCCB_ADDR, true) < 0)
    {
      return 1;
    }

  memset(&cfg, 0, sizeof(cfg));
  cfg.lanes_num = CONFIG_EXAMPLES_CAMERA_DIAG_CSI_LANES;
  cfg.frame_width = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH;
  cfg.frame_height = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT;
  cfg.in_bpp = 16;
  cfg.out_bpp = 16;
  cfg.lane_bit_rate_mbps = CONFIG_EXAMPLES_CAMERA_DIAG_LANE_RATE_MBPS;
  ret = esp32p4_csi_init(&cfg);
  if (ret != OK)
    {
      goto stream_off;
    }

  memset(&isp_cfg, 0, sizeof(isp_cfg));
  isp_cfg.frame_width = cfg.frame_width;
  isp_cfg.frame_height = cfg.frame_height;
  isp_cfg.clk_hz = 80000000;
  ret = esp32p4_csi_isp_init(&isp_cfg);
  if (ret != OK || esp32p4_csi_install_block_isr() != OK)
    {
      goto csi_off;
    }

  esp32p4_csi_brg_set_rgb565_out();
  esp32p4_csi_enable();
  ret = esp32p4_dsi_live_start(g_camera_frame_rgb565,
                               CAMERA_DIAG_FRAME_BYTES_RGB565);
  if (ret != OK)
    {
      printf("camera_diag: live DSI start failed: %d\n", ret);
      goto csi_off;
    }

  if (duration_ms != 0)
    {
      esp32p4_csi_set_runtime_diag(false);
      printf("camera_diag: live stability started duration_ms=%lu\n",
             (unsigned long)duration_ms);
    }

  start_ms = (uint32_t)((clock_systime_ticks() * 1000UL) / TICK_PER_SEC);
  last_report_ms = start_ms;
  esp32p4_csi_bridge_enable();
  for (frame_index = 0;
       duration_ms == 0 ? frame_index < target_frames :
                          ((uint32_t)((clock_systime_ticks() * 1000UL) /
                                      TICK_PER_SEC) - start_ms) < duration_ms;
       frame_index++)
    {
      frame_len = esp32p4_csi_capture_one_rgb565(
        g_camera_frame_rgb565, CAMERA_DIAG_FRAME_BYTES_RGB565, &ts_ms);
      if (frame_len != CAMERA_DIAG_FRAME_BYTES_RGB565)
        {
          printf("camera_diag: live frame %d/%d failed: %d\n",
                 frame_index + 1, target_frames, frame_len);
          ret = -EIO;
          break;
        }

      if (duration_ms == 0)
        {
          printf("camera_diag: live frame %d/%d len=%d ts_ms=%lu\n",
                 frame_index + 1, target_frames, frame_len,
                 (unsigned long)ts_ms);
        }
      else if (ts_ms - last_report_ms >= 1000)
        {
          printf("camera_diag: live stability frames=%d elapsed_ms=%lu\n",
                 frame_index + 1, (unsigned long)(ts_ms - start_ms));
          last_report_ms = ts_ms;
        }
    }

  esp32p4_csi_bridge_disable();
  esp32p4_csi_set_runtime_diag(true);
  esp32p4_dsi_live_stop();
  esp32p4_csi_disable();
  esp32p4_csi_isp_deinit();
  sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
  if (ret == OK)
    {
      if (duration_ms == 0)
        {
          printf("camera_diag: live preview %d/%d completed\n",
                 target_frames, target_frames);
        }
      else
        {
          printf("camera_diag: live stability completed frames=%d elapsed_ms=%lu\n",
                 frame_index, (unsigned long)(ts_ms - start_ms));
        }
      return 0;
    }

  return 1;

csi_off:
  esp32p4_csi_set_runtime_diag(true);
  esp32p4_csi_disable();
  esp32p4_csi_isp_deinit();
stream_off:
  sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
  return 1;
}

static int csi_data_diag(void)
{
  FAR struct i2c_master_s *i2c;
  struct esp32p4_csi_config_s cfg;
  uint16_t pid = 0;
  int ret;

  /* 1. I2C bus (SC2336 SCCB control) */
  i2c = esp32p4_i2c_bus_initialize(CONFIG_EXAMPLES_CAMERA_DIAG_I2C_PORT,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SCL_PIN,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SDA_PIN);
  if (i2c == NULL)
    {
      fprintf(stderr, "camera_diag: I2C init failed\n");
      return 1;
    }

  printf("camera_diag: i2c ok\n");

  /* 2. SC2336 probe */
  ret = sc2336_probe(i2c, SC2336_SCCB_ADDR, &pid);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 probe failed: %d\n", ret);
      return 1;
    }

  if (pid != SC2336_PID)
    {
      fprintf(stderr, "camera_diag: unexpected PID 0x%04x (expect 0x%04x)\n",
              pid, SC2336_PID);
      return 1;
    }

  printf("camera_diag: sc2336 pid=0x%04x\n", pid);

  /* 3. SC2336 init + stream on */
  ret = sc2336_init(i2c, SC2336_SCCB_ADDR);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 init failed: %d\n", ret);
      return 1;
    }

  ret = sc2336_stream(i2c, SC2336_SCCB_ADDR, true);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 stream-on failed: %d\n", ret);
      return 1;
    }

  printf("camera_diag: sc2336 streaming (%ux%u RAW8 %u Mbps/lane)\n",
         CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH,
         CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT,
         CONFIG_EXAMPLES_CAMERA_DIAG_LANE_RATE_MBPS);

  /* 4. CSI bridge/host init only - no GDMA alloc/start (L1.8.17) */
  memset(&cfg, 0, sizeof(cfg));
  cfg.lanes_num        = CONFIG_EXAMPLES_CAMERA_DIAG_CSI_LANES;
  cfg.frame_width      = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH;
  cfg.frame_height     = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT;
  cfg.in_bpp           = 8;
  cfg.out_bpp          = 8;
  cfg.byte_swap_en     = false;
  cfg.lane_bit_rate_mbps = CONFIG_EXAMPLES_CAMERA_DIAG_LANE_RATE_MBPS;

  ret = esp32p4_csi_brg_init(&cfg);
  if (ret != OK)
    {
      fprintf(stderr, "camera_diag: CSI bridge init failed: %d\n", ret);
      sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
      return 1;
    }

  /* 5. Data-plane diagnosis: enable bridge (no DMA), wait 500 ms, dump
   *    bridge FIFO depth / data-lane stop-state / host error state, then
   *    disable bridge. No frame capture, no image save. */
  printf("camera_diag: csi data-plane diag (bridge en, no DMA, no capture)\n");
  ret = esp32p4_csi_data_diag();
  if (ret != OK)
    {
      fprintf(stderr, "camera_diag: CSI data diag failed: %d\n", ret);
    }

  /* 6. Stream off and exit */
  sc2336_stream(i2c, SC2336_SCCB_ADDR, false);
  /* L1.8.25f R2: release the CSI bridge claim taken by
   * esp32p4_csi_brg_init() (no ISP in this diag). */
  esp32p4_csi_disable();
  printf("camera_diag: stream off\n");
  return ret == OK ? 0 : 1;
}

int main(int argc, FAR char *argv[])
{
  FAR struct i2c_master_s *i2c;
  struct esp32p4_csi_config_s csi_cfg;
  FAR uint8_t *frame;
  char hash_hex[65];
  uint16_t pid = 0;
  uint32_t ts_ms = 0;
  int frame_len;
  int ret;

  if (argc > 1)
    {
      if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
        {
          print_usage(stdout);
          return 0;
        }

      if (strcmp(argv[1], "--model-storage") == 0)
        {
          return camera_diag_model_storage_diag();
        }

      if (strcmp(argv[1], "--espdl-storage") == 0)
        {
          return camera_diag_espdl_storage_diag();
        }

      if (strcmp(argv[1], "--tflm-init") == 0)
        {
          return camera_diag_tflm_init_diag();
        }

      if (strcmp(argv[1], "--tflm-nms-selftest") == 0)
        {
          return camera_diag_tflm_nms_selftest();
        }

      if (strcmp(argv[1], "--result-ui-selftest") == 0)
        {
          if (!esp32p4_dsi_status_ui_is_active() &&
              esp32p4_dsi_status_ui_start() != OK)
            {
              return 1;
            }

          ret = esp32p4_dsi_status_ui_set_inference_result(2);
          printf("camera_diag: result UI selftest rc=%d candidates=2\n", ret);
          return ret == OK ? 0 : 1;
        }

      if (strcmp(argv[1], "--energy-ui-selftest") == 0)
        {
          if (!esp32p4_dsi_status_ui_is_active() &&
              esp32p4_dsi_status_ui_start() != OK)
            {
              return 1;
            }

          ret = esp32p4_dsi_status_ui_set_inspection_result(1, 3);
          printf("camera_diag: energy UI selftest rc=%d defects=1 "
                 "energy_level=3\n", ret);
          return ret == OK ? 0 : 1;
        }

      if (strcmp(argv[1], "--probe") == 0)
        {
          return probe_pid_only();
        }

      if (strcmp(argv[1], "--i2c-diag") == 0)
        {
          return i2c_diag();
        }

      if (strcmp(argv[1], "--psram-check") == 0)
        {
          return psram_check();
        }

      if (strcmp(argv[1], "--csi-diag") == 0)
        {
          /* L1.8.7: CSI + DW-GDMA register dump (same init config as the
           * capture path, but no sensor, no enable, no start). */
          struct esp32p4_csi_config_s cfg;

          memset(&cfg, 0, sizeof(cfg));
          cfg.lanes_num        = CONFIG_EXAMPLES_CAMERA_DIAG_CSI_LANES;
          cfg.frame_width      = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH;
          cfg.frame_height     = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT;
          cfg.in_bpp           = 8;
          cfg.out_bpp          = 8;
          cfg.byte_swap_en     = false;
          cfg.lane_bit_rate_mbps = CONFIG_EXAMPLES_CAMERA_DIAG_LANE_RATE_MBPS;

          printf("camera_diag: csi init\n");
          if (esp32p4_csi_init(&cfg) != OK)
            {
              fprintf(stderr, "camera_diag: csi init failed\n");
              return 1;
            }

          printf("camera_diag: csi/gdma register dump\n");
          ret = esp32p4_csi_diag();
          /* L1.8.25f R2: release the CSI bridge claim (RAW8 path, no ISP). */
          esp32p4_csi_disable();
          return ret;
        }

      if (strcmp(argv[1], "--csi-data-diag") == 0)
        {
          return csi_data_diag();
        }

      if (strcmp(argv[1], "--csi-runtime-diag") == 0)
        {
          /* L1.8.8: stream SC2336, init CSI bridge/host only (no GDMA),
           * dump phy_rx/host-intr/bridge after a short window, stream off. */
          return csi_runtime_diag();
        }

      if (strcmp(argv[1], "--isp-frame") == 0)
        {
          return isp_frame_diag();
        }

      if (strcmp(argv[1], "--isp-dsi-preview") == 0)
        {
          return isp_dsi_preview();
        }

      if (strcmp(argv[1], "--isp-dsi-burst") == 0)
        {
          return isp_dsi_preview_burst();
        }

      if (strcmp(argv[1], "--isp-dsi-live") == 0)
        {
          return isp_dsi_live(0);
        }

      if (strcmp(argv[1], "--isp-dsi-stability") == 0)
        {
          return isp_dsi_live(60000);
        }

      if (strcmp(argv[1], "--dsi-pattern") == 0)
        {
          /* L1.8.25f display Stage 1: EK79007 DSI panel solid-color / color
           * bar diagnostic. Optional mode arg: 0=red 1=green 2=8-bar.
           * No camera, no capture, no network. */
          int mode = 2;

          if (argc > 2)
            {
              mode = atoi(argv[2]);
            }

          printf("camera_diag: dsi-pattern mode=%d\n", mode);
          return esp32p4_dsi_pattern_diag(mode, 0) == OK ? 0 : 1;
        }

      if (strcmp(argv[1], "--dsi-ui") == 0)
        {
          printf("camera_diag: dsi minimal status UI\n");
          return esp32p4_dsi_status_ui_diag(0) == OK ? 0 : 1;
        }

      if (strcmp(argv[1], "--dsi-ui-live") == 0)
        {
          printf("camera_diag: starting persistent status UI\n");
          return esp32p4_dsi_status_ui_start() == OK ? 0 : 1;
        }

      if (strcmp(argv[1], "--touch-diag") == 0)
        {
          return touch_diag(10000, false);
        }

      if (strcmp(argv[1], "--tflm-validation-sample") == 0)
        {
          const uint8_t *sample;
          const uint8_t *sample_end;
          int rc;
          int defects;

          if (argc != 3)
            {
              fprintf(stderr, "camera_diag: validation sample is stain, damage, or wrinkle\n");
              return 2;
            }

          if (strcmp(argv[2], "stain") == 0)
            {
              sample = g_m13_sample_stain;
              sample_end = g_m13_sample_stain_end;
            }
          else if (strcmp(argv[2], "damage") == 0)
            {
              sample = g_m13_sample_damage;
              sample_end = g_m13_sample_damage_end;
            }
          else if (strcmp(argv[2], "wrinkle") == 0)
            {
              sample = g_m13_sample_wrinkle;
              sample_end = g_m13_sample_wrinkle_end;
            }
          else
            {
              fprintf(stderr, "camera_diag: unknown validation sample '%s'\n", argv[2]);
              return 2;
            }

          printf("camera_diag: ESP validation sample=%s bytes=%lu\n", argv[2],
                 (unsigned long)(sample_end - sample));
          rc = camera_diag_tflm_preprocess_diag(sample,
                                                 (size_t)(sample_end - sample),
                                                 256, 256);
          defects = camera_diag_tflm_last_defect_nms_count();
          if (rc == 0 && defects >= 0 && esp32p4_dsi_status_ui_is_active())
            {
              (void)esp32p4_dsi_status_ui_set_inspection_result(
                defects, camera_diag_tflm_last_energy_level());
              (void)esp32p4_dsi_status_ui_set_label_detected(
                camera_diag_tflm_last_label_detected());
            }

          printf("camera_diag: ESP validation sample=%s rc=%d defects=%d\n",
                 argv[2], rc, defects);
          return rc;
        }

      if (strcmp(argv[1], "--touch-ui") == 0 ||
          strcmp(argv[1], "--touch-ui-live") == 0 ||
          strcmp(argv[1], "--touch-ui-live-delayed") == 0)
        {
          bool touch_ui_live = strcmp(argv[1], "--touch-ui") != 0;

          if (strcmp(argv[1], "--touch-ui-live-delayed") == 0)
            {
              printf("camera_diag: delaying status UI for panel stability\n");
              nxsig_usleep(10000000);
            }

          if (!esp32p4_dsi_status_ui_is_active())
            {
              printf("camera_diag: starting persistent status UI for touch feedback\n");
              ret = esp32p4_dsi_status_ui_start();
              if (ret != OK)
                {
                  fprintf(stderr, "camera_diag: status UI start failed: %d\n", ret);
                  return 1;
                }
            }

          if (touch_ui_live)
            {
              esp32p4_dsi_set_workflow_callback(workflow_button_handler);
            }
          return touch_diag(touch_ui_live ? -1 : 300000, true);
        }

      fprintf(stderr, "camera_diag: unknown option '%s'\n", argv[1]);
      print_usage(stderr);
      return 2;
    }

  /* 1. I2C bus (SC2336 SCCB control; schematic-verified SCL=GPIO8/SDA=GPIO7) */
  i2c = esp32p4_i2c_bus_initialize(CONFIG_EXAMPLES_CAMERA_DIAG_I2C_PORT,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_FREQ,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SCL_PIN,
                                   CONFIG_EXAMPLES_CAMERA_DIAG_I2C_SDA_PIN);
  if (i2c == NULL)
    {
      fprintf(stderr, "camera_diag: I2C init failed\n");
      return 1;
    }

  printf("camera_diag: i2c ok\n");

  /* 2. SC2336 probe */
  ret = sc2336_probe(i2c, SC2336_SCCB_ADDR, &pid);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 probe failed: %d\n", ret);
      return 1;
    }

  if (pid != SC2336_PID)
    {
      fprintf(stderr, "camera_diag: sensor PID=0x%04x (expected 0x%04x)\n",
              pid, SC2336_PID);
      return 1;
    }

  printf("camera_diag: sc2336 detected pid=0x%04x\n", pid);

  /* 3. SC2336 init + stream on */
  ret = sc2336_init(i2c, SC2336_SCCB_ADDR);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 init failed: %d\n", ret);
      return 1;
    }

  ret = sc2336_stream(i2c, SC2336_SCCB_ADDR, true);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: SC2336 stream-on failed: %d\n", ret);
      return 1;
    }

  printf("camera_diag: sc2336 initialized (%ux%u RAW8 30fps)\n",
         (unsigned int)CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH,
         (unsigned int)CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT);

  /* 4. Frame buffer: static PSRAM (.ext_ram.bss), explicit data path */
  frame = g_camera_frame;

  /* 5. CSI + DW-GDMA config */
  memset(&csi_cfg, 0, sizeof(csi_cfg));
  csi_cfg.lanes_num          = CONFIG_EXAMPLES_CAMERA_DIAG_CSI_LANES;
  csi_cfg.frame_width        = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH;
  csi_cfg.frame_height       = CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT;
  csi_cfg.in_bpp             = CONFIG_EXAMPLES_CAMERA_DIAG_BPP;
  csi_cfg.out_bpp            = CONFIG_EXAMPLES_CAMERA_DIAG_BPP;
  csi_cfg.byte_swap_en       = false;
  csi_cfg.lane_bit_rate_mbps = CONFIG_EXAMPLES_CAMERA_DIAG_LANE_RATE_MBPS;

  ret = esp32p4_csi_init(&csi_cfg);
  if (ret < 0)
    {
      fprintf(stderr, "camera_diag: CSI init failed: %d\n", ret);
      return 1;
    }

  printf("camera_diag: csi+dwgdma configured\n");

  /* 6. Capture one frame */
  esp32p4_csi_enable();
  frame_len = esp32p4_csi_capture_one(frame, CAMERA_DIAG_FRAME_BYTES, &ts_ms);
  esp32p4_csi_disable();

  if (frame_len < 0)
    {
      fprintf(stderr, "camera_diag: capture failed: %d\n", frame_len);
      return 1;
    }

  sha256_hex(frame, (size_t)frame_len, hash_hex);
  printf("camera_diag frame len=%d w=%u h=%u fmt=RAW8 status=OK ts_ms=%lu "
         "sha256=%s\n",
         frame_len,
         (unsigned int)CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH,
         (unsigned int)CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT,
         (unsigned long)ts_ms, hash_hex);

  printf("camera_diag: capture chain OK\n");
  return 0;
}
