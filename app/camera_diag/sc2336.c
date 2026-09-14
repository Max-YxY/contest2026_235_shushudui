/****************************************************************************
 * apps/examples/camera_diag/sc2336.c
 *
 * S11-L1 camera build integration: minimal SC2336 sensor driver over NuttX
 * I2C (SCCB 16-bit register addressing, 7-bit slave address 0x30).
 * Register tables extracted from Espressif esp_cam_sensor 2.3.0
 * (Apache-2.0). Default mode: MIPI 2-lane 24M input 1280x720 RAW8 30fps.
 *
 * Controlled camera integration; not real label inspection. Build-level
 * integration only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <debug.h>

#include "sc2336.h"
#include "private_include/sc2336_regs.h"
#include "private_include/sc2336_mipi_2lane_24Minput_1280x720_raw8_30fps.h"
#include "private_include/sc2336_mipi_2lane_24Minput_1024x600_raw8_30fps.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int sc2336_sccb_read(FAR struct i2c_master_s *i2c, uint8_t addr,
                            uint16_t reg, FAR uint8_t *value)
{
  struct i2c_msg_s msgs[2];
  uint8_t reg_buf[2];
  int ret;

  reg_buf[0] = (uint8_t)(reg >> 8);
  reg_buf[1] = (uint8_t)(reg & 0xff);

  msgs[0].frequency = I2C_SPEED_FAST;
  msgs[0].addr      = addr;
  msgs[0].flags     = 0;
  msgs[0].buffer    = reg_buf;
  msgs[0].length    = 2;

  msgs[1].frequency = I2C_SPEED_FAST;
  msgs[1].addr      = addr;
  msgs[1].flags     = I2C_M_READ;
  msgs[1].buffer    = value;
  msgs[1].length    = 1;

  ret = I2C_TRANSFER(i2c, msgs, 2);
  if (ret < 0)
    {
      i2cerr("sc2336 read reg 0x%04x failed: %d\n", reg, ret);
    }

  return ret;
}

static int sc2336_sccb_write(FAR struct i2c_master_s *i2c, uint8_t addr,
                             uint16_t reg, uint8_t value)
{
  struct i2c_msg_s msg;
  uint8_t buf[3];
  int ret;

  buf[0] = (uint8_t)(reg >> 8);
  buf[1] = (uint8_t)(reg & 0xff);
  buf[2] = value;

  msg.frequency = I2C_SPEED_FAST;
  msg.addr      = addr;
  msg.flags     = 0;
  msg.buffer    = buf;
  msg.length    = 3;

  ret = I2C_TRANSFER(i2c, &msg, 1);
  if (ret < 0)
    {
      i2cerr("sc2336 write reg 0x%04x = 0x%02x failed: %d\n",
             reg, value, ret);
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sc2336_probe(FAR struct i2c_master_s *i2c, uint8_t addr, uint16_t *pid)
{
  uint8_t pid_h;
  uint8_t pid_l;
  int ret;

  ret = sc2336_sccb_read(i2c, addr, SC2336_REG_SENSOR_ID_H, &pid_h);
  if (ret < 0)
    {
      return ret;
    }

  ret = sc2336_sccb_read(i2c, addr, SC2336_REG_SENSOR_ID_L, &pid_l);
  if (ret < 0)
    {
      return ret;
    }

  if (pid != NULL)
    {
      *pid = (uint16_t)(((uint16_t)pid_h << 8) | pid_l);
    }

  i2cinfo("sc2336 id = 0x%04x\n", (uint16_t)(((uint16_t)pid_h << 8) | pid_l));
  return OK;
}

int sc2336_init(FAR struct i2c_master_s *i2c, uint8_t addr)
{
  /* L1.8.14 F2: select the official SC2336 mode table from the diagnostic
   * resolution (default aligned to official 1024x600 RAW8 30fps). */
#if CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_WIDTH == 1024 && \
    CONFIG_EXAMPLES_CAMERA_DIAG_FRAME_HEIGHT == 600
  const sc2336_reginfo_t *reg = sc2336_mipi_2lane_24Minput_1024x600_raw8_30fps;
#else
  const sc2336_reginfo_t *reg = sc2336_mipi_2lane_24Minput_1280x720_raw8_30fps;
#endif
  int ret;

  for (; reg->reg != SC2336_REG_END; reg++)
    {
      if (reg->reg == SC2336_REG_DELAY)
        {
          continue;
        }

      ret = sc2336_sccb_write(i2c, addr, reg->reg, reg->val);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

int sc2336_stream(FAR struct i2c_master_s *i2c, uint8_t addr, bool enable)
{
  return sc2336_sccb_write(i2c, addr, SC2336_REG_SLEEP_MODE,
                           enable ? 0x01 : 0x00);
}
