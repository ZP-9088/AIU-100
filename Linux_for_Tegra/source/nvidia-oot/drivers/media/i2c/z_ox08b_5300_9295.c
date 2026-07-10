/*
 * z_ox08b_5300_9295.c - OX08B40 + GW5300 ISP + MAX9295A sensor driver
 *                       for zuojisi MAX96724 framework
 *
 * Camera module: SG8-OX08BC-5300-GMSL2
 *   Sensor: OmniVision OX08B40 (I2C 0x36/7bit, behind ISP)
 *   ISP:    GW5300 (I2C 0x6D/7bit, reset via MAX9295A MFP0)
 *   SerDes: MAX9295A (I2C 0x40/7bit)
 *   Output: YUV422 8bit, 3840x2160@30fps
 *   Frame Sync: MAX9295A MFP8
 *
 * Based on z_imx390_5200_9295.c
 * zuojisi@163.com
 */

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include "z_max96724.h"

#include <media/tegracam_core.h>
#include "z_ox08b_5300_9295_mode_tbls.h"

#define MAX9295_ALTER_ADDR_BASE 0x20

/* MAX9295A MFP register base addresses (3 regs per MFP) */
#define MAX9295_MFP0_BASE  0x02BE
#define MAX9295_MFP7_BASE  0x02D3  /* Camera trigger: MFP7 per reference script */
#define MAX9295_MFP8_BASE  0x02D6  /* Frame sync: MFP8 per module spec */

const struct of_device_id z_ox08b_5300_9295_of_match[] = {
	{ .compatible = "zuojisi,z_ox08b_5300_9295",},
	{ },
};
MODULE_DEVICE_TABLE(of, z_ox08b_5300_9295_of_match);

static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

struct z_ox08b_5300_9295 {
	struct i2c_client	*i2c_client;
	const struct i2c_device_id *id;
	struct v4l2_subdev	*subdev;
	struct device		*dser_dev;
	struct camera_common_data	*s_data;
	struct tegracam_device		*tc_dev;
	struct gmsl_link_ctx	g_ctx;
	u32 def_addr;
	u32 act_addr;
	u32 des_link;
	bool streaming_initialized;  /* tracks if video pipeline was initialized */
};

static const struct regmap_config sensor_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
	.use_single_rw = true,
#else
	.use_single_read = true,
	.use_single_write = true,
#endif
};

static struct mutex serdes_lock__;

static int test_mode;
module_param(test_mode, int, 0644);

static int z_ox08b_5300_9295_read_reg(struct camera_common_data *s_data,
				u16 addr, u8 *val)
{
	int err = 0;
	u32 reg_val = 0;
	struct device *dev = s_data->dev;

	err = regmap_read(s_data->regmap, addr, &reg_val);
	usleep_range(100, 110);
	if (!err) {
		*val = reg_val & 0xFF;
		dev_info(dev, "zuojisi:z_ox08b_5300_9295:r addr(0x%04x)<-0x%02x\n", addr, *val);
	}
	if (err)
		dev_err(dev, "zuojisi:z_ox08b_5300_9295: i2c read failed, addr=0x%x\n", addr);

	return err;
}

static int z_ox08b_5300_9295_write_reg(struct camera_common_data *s_data,
				u16 addr, u8 val)
{
	int err;
	struct device *dev = s_data->dev;

	err = regmap_write(s_data->regmap, addr, val);
	if (err)
		dev_err(dev, "zuojisi:%s:z_ox08b_5300_9295:i2c write failed, 0x%x = %x\n",
			__func__, addr, val);

	usleep_range(100, 110);
	if (!err)
		dev_info(dev, "zuojisi:z_ox08b_5300_9295:w addr(0x%04x)->0x%02x\n", addr, val);
	return err;
}

static int iic_write(struct i2c_client *client, u8 slaveaddr, u16 regaddr, u8 data)
{
	struct i2c_msg msg[1];
	u8 buf[3];
	int ret;

	msg[0].addr = slaveaddr;
	msg[0].flags = 0;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	buf[0] = regaddr >> 8;
	buf[1] = regaddr & 0xff;
	buf[2] = data;
	ret = i2c_transfer(client->adapter, msg, 1);
	usleep_range(100, 110);
	if (ret == 1) {
		dev_info(&client->dev, "zuojisi:z_ox08b_5300_9295:w slave=0x%02x addr(0x%04x)->0x%02x\n",
			slaveaddr, regaddr, data);
		return 0;
	} else {
		dev_err(&client->dev, "zuojisi:%s:z_ox08b_5300_9295:i2c write failed, slave=0x%02x addr(0x%04x)->0x%02x\n",
			__func__, slaveaddr, regaddr, data);
		return -1;
	}
}

static int iic_read(struct i2c_client *client, u8 slaveaddr, u16 regaddr, u8 *data)
{
	struct i2c_msg msg[2];
	u8 buf[3];
	int ret;

	msg[0].addr = slaveaddr;
	msg[0].flags = 0;
	msg[0].buf = buf;
	msg[0].len = 2;

	buf[0] = regaddr >> 8;
	buf[1] = regaddr & 0xff;

	msg[1].addr = slaveaddr;
	msg[1].buf = buf;
	msg[1].len = 1;
	msg[1].flags = I2C_M_RD;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret == 2) {
		*data = buf[0];
		dev_info(&client->dev, "zuojisi:z_ox08b_5300_9295:r slave=0x%02x addr(0x%04x)<-0x%02x\n",
			slaveaddr, regaddr, *data);
		return 0;
	} else {
		dev_err(&client->dev, "zuojisi: %s z_ox08b_5300_9295: i2c read failed, slave=0x%02x, addr=0x%04x\n",
			__func__, slaveaddr, regaddr);
		return -1;
	}
}

static int z_ox08b_5300_9295_power_on(struct camera_common_data *s_data)
{
	struct camera_common_power_rail *pw = s_data->power;

	pw->state = SWITCH_ON;
	return 0;
}

static int z_ox08b_5300_9295_power_off(struct camera_common_data *s_data)
{
	struct camera_common_power_rail *pw = s_data->power;

	pw->state = SWITCH_OFF;
	return 0;
}

static int z_ox08b_5300_9295_power_get(struct tegracam_device *tc_dev)
{
	struct camera_common_power_rail *pw = tc_dev->s_data->power;

	pw->state = SWITCH_OFF;
	return 0;
}

static int z_ox08b_5300_9295_power_put(struct tegracam_device *tc_dev)
{
	return 0;
}

static int z_ox08b_5300_9295_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	dev_info(tc_dev->dev, "zuojisi: %s\n", __func__);
	return 0;
}

static int z_ox08b_5300_9295_set_gain(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = s_data->dev;

	dev_info(dev, "zuojisi: %s, gain=%lld\n", __func__, val);
	/* GW5300 ISP handles gain control internally */
	return 0;
}

static int z_ox08b_5300_9295_set_exposure(struct tegracam_device *tc_dev, s64 val)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = s_data->dev;

	dev_info(dev, "zuojisi: %s, exp=%lld\n", __func__, val);
	/* GW5300 ISP handles exposure control internally */
	return 0;
}

static int z_ox08b_5300_9295_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	dev_info(tc_dev->dev, "zuojisi: %s, framerate=%lld\n", __func__, val);
	return 0;
}

static struct tegracam_ctrl_ops z_ox08b_5300_9295_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.set_gain = z_ox08b_5300_9295_set_gain,
	.set_exposure = z_ox08b_5300_9295_set_exposure,
	.set_frame_rate = z_ox08b_5300_9295_set_frame_rate,
	.set_group_hold = z_ox08b_5300_9295_set_group_hold,
};

static struct camera_common_pdata *z_ox08b_5300_9295_parse_dt(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct device_node *node = dev->of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;

	if (!node)
		return NULL;

	match = of_match_device(z_ox08b_5300_9295_of_match, dev);
	if (!match) {
		dev_err(dev, "Failed to find matching dt id\n");
		return NULL;
	}

	board_priv_pdata = devm_kzalloc(dev, sizeof(*board_priv_pdata), GFP_KERNEL);

	return board_priv_pdata;
}

static int z_ox08b_5300_9295_set_mode(struct tegracam_device *tc_dev)
{
	dev_info(tc_dev->dev, "zuojisi:z_ox08b_5300_9295 set mode\n");
	/* GW5300 ISP handles mode setting internally, no register writes needed */
	return 0;
}

static int z_ox08b_5300_9295_start_streaming(struct tegracam_device *tc_dev)
{
	struct z_ox08b_5300_9295 *priv = (struct z_ox08b_5300_9295 *)tegracam_get_privdata(tc_dev);
	struct device *dev = tc_dev->dev;
	u8 ser_addr = MAX9295_ALTER_ADDR_BASE + priv->des_link;
	int err;

	mutex_lock(&serdes_lock__);
	dev_info(dev, "zuojisi: z_ox08b_5300_9295 start streaming - enter\n");

	err = z_max96724_start_streaming(priv->dser_dev, &priv->g_ctx);
	if (err)
		goto exit;

	/*
	 * MAX9295A configuration sequence.
	 * On first start: full reset and configure.
	 * On hot restart: only re-enable video output (skip RESET_ONESHOT and MFP triggers).
	 */
	if (!priv->streaming_initialized) {
		/* First start: full reset sequence */
		dev_info(dev, "zuojisi: MAX9295A first start, full init\n");

		/*
		 * Note: Reference script does NOT use RESET_ONESHOT.
		 * Removing it to match reference behavior.
		 */

		/* Step 1: Disable video output first */
		iic_write(priv->i2c_client, ser_addr, 0x0002, 0x03); /* disable video */
		iic_write(priv->i2c_client, ser_addr, 0x0311, 0x00); /* stop z-pipe */
		iic_write(priv->i2c_client, ser_addr, 0x0308, 0x60); /* disable CSI-B */
		mdelay(10);

		/*
		 * Step 2: Configure MFP0 and release GW5300 ISP from reset.
		 * MFP0 controls RCLK output to ISP. Setting 0x02be=0x10 releases reset.
		 * The ISP needs significant time (>100ms) to initialize after reset release.
		 */
		dev_info(dev, "zuojisi: releasing GW5300 ISP from reset via MFP0\n");
		iic_write(priv->i2c_client, ser_addr, 0x02bf, 0x60);
		iic_write(priv->i2c_client, ser_addr, 0x02be, 0x10); /* MFP0 high = ISP active */
		mdelay(150);  /* Wait for ISP to initialize - critical! */

		/* Step 3: Configure video pipeline */
		iic_write(priv->i2c_client, ser_addr, 0x0318, 0x5e); /* video pipe config */
		iic_write(priv->i2c_client, ser_addr, 0x0308, 0x64); /* enable CSI-B */
		iic_write(priv->i2c_client, ser_addr, 0x0311, 0x40); /* start z from CSI-B */
		iic_write(priv->i2c_client, ser_addr, 0x0002, 0x43); /* enable z video */
		mdelay(50);  /* Wait for video pipeline to stabilize */

		/*
		 * Camera trigger via MFP7 (per reference script)
		 * MFP7 register base = 0x02D3
		 * This triggers the GW5300 ISP to start outputting video.
		 */
		dev_info(dev, "zuojisi: triggering GW5300 via MFP7 low-to-high pulse\n");
		iic_write(priv->i2c_client, ser_addr, MAX9295_MFP7_BASE, 0x00); /* MFP7 low */
		mdelay(300); /* Reference script uses 300ms */
		iic_write(priv->i2c_client, ser_addr, MAX9295_MFP7_BASE, 0x10); /* MFP7 high */
		mdelay(50);

		priv->streaming_initialized = true;
	} else {
		/*
		 * Hot restart: ISP is still running, re-enable video output
		 * and send a sync pulse to re-synchronize the video stream.
		 */
		dev_info(dev, "zuojisi: MAX9295A hot restart, re-enable video\n");
		
		/* Re-enable video output registers */
		iic_write(priv->i2c_client, ser_addr, 0x0308, 0x64); /* enable CSI-B */
		iic_write(priv->i2c_client, ser_addr, 0x0311, 0x40); /* start z from CSI-B */
		iic_write(priv->i2c_client, ser_addr, 0x0002, 0x43); /* enable z video */
		mdelay(50);  /* Wait for video pipeline to stabilize */

		/*
		 * Send a short MFP7 pulse to re-sync ISP video output.
		 * This is needed because stopping the video stream may have
		 * desynchronized the ISP frame timing.
		 */
		dev_info(dev, "zuojisi: hot restart - sending MFP7 sync pulse\n");
		iic_write(priv->i2c_client, ser_addr, MAX9295_MFP7_BASE, 0x00); /* MFP7 low */
		mdelay(100); /* Shorter delay for hot restart */
		iic_write(priv->i2c_client, ser_addr, MAX9295_MFP7_BASE, 0x10); /* MFP7 high */
		mdelay(50);
	}

	/* Diagnostic reads to verify video pipeline status */
	{
		u8 val;
		if (iic_read(priv->i2c_client, ser_addr, 0x0002, &val) == 0)
			dev_info(dev, "zuojisi: MAX9295A VIDEO_TX (0x0002) = 0x%02x\n", val);
		if (iic_read(priv->i2c_client, ser_addr, 0x0311, &val) == 0)
			dev_info(dev, "zuojisi: MAX9295A PIPE_EN (0x0311) = 0x%02x\n", val);
		if (iic_read(priv->i2c_client, ser_addr, 0x0308, &val) == 0)
			dev_info(dev, "zuojisi: MAX9295A CSI_PORT (0x0308) = 0x%02x\n", val);
		if (iic_read(priv->i2c_client, ser_addr, 0x02be, &val) == 0)
			dev_info(dev, "zuojisi: MAX9295A MFP0 (0x02be) = 0x%02x (ISP reset state)\n", val);
	}

	dev_info(dev, "zuojisi: z_ox08b_5300_9295 start streaming - exit\n");
	mutex_unlock(&serdes_lock__);
	return 0;

exit:
	mutex_unlock(&serdes_lock__);
	if (err)
		dev_err(dev, "zuojisi: %s: error setting stream\n", __func__);
	dev_info(dev, "zuojisi: z_ox08b_5300_9295 start streaming - exit\n");
	return err;
}

static int z_ox08b_5300_9295_stop_streaming(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct z_ox08b_5300_9295 *priv = (struct z_ox08b_5300_9295 *)tegracam_get_privdata(tc_dev);
	u8 ser_addr = MAX9295_ALTER_ADDR_BASE + priv->des_link;
	u8 val;

	dev_info(dev, "zuojisi:z_ox08b_5300_9295 stop streaming - enter, link_%c\n",
		'A' + priv->des_link);

	/*
	 * Disable MAX9295A video output and reset GW5300 ISP.
	 * ISP must be fully reset so next start_streaming gets clean frames.
	 * Without reset, ISP continues free-running and re-start catches mid-frame
	 * causing horizontal/vertical sync errors on Tegra CSI.
	 */
	iic_write(priv->i2c_client, ser_addr, 0x0002, 0x03); /* disable video output */
	iic_write(priv->i2c_client, ser_addr, 0x0311, 0x00); /* stop z-pipe */
	iic_write(priv->i2c_client, ser_addr, 0x0308, 0x60); /* disable CSI-B output */
	mdelay(10);

	/* Hold GW5300 ISP in reset via MFP0 low */
	iic_write(priv->i2c_client, ser_addr, 0x02bf, 0x60);
	iic_write(priv->i2c_client, ser_addr, 0x02be, 0x00); /* MFP0 low = ISP reset */
	dev_info(dev, "zuojisi: GW5300 ISP held in reset via MFP0\n");

	/* Read back and verify MAX9295A state */
	if (iic_read(priv->i2c_client, ser_addr, 0x0002, &val) == 0) {
		dev_info(dev, "zuojisi: MAX9295A reg 0x0002 = 0x%02x after stop\n", val);
	}

	/*
	 * Force full re-initialization on next start.
	 * This ensures MFP0 release + MFP7 trigger sequence runs again.
	 */
	priv->streaming_initialized = false;
	dev_info(dev, "zuojisi: streaming_initialized=false, next start will do full init\n");

	z_max96724_stop_streaming(priv->dser_dev, &priv->g_ctx);

	dev_info(dev, "zuojisi:z_ox08b_5300_9295 stop streaming - exit\n");
	return 0;
}

static struct camera_common_sensor_ops z_ox08b_5300_9295_common_ops = {
	.numfrmfmts = ARRAY_SIZE(z_ox08b_5300_9295_frmfmt),
	.frmfmt_table = z_ox08b_5300_9295_frmfmt,
	.power_on = z_ox08b_5300_9295_power_on,
	.power_off = z_ox08b_5300_9295_power_off,
	.write_reg = z_ox08b_5300_9295_write_reg,
	.read_reg = z_ox08b_5300_9295_read_reg,
	.parse_dt = z_ox08b_5300_9295_parse_dt,
	.power_get = z_ox08b_5300_9295_power_get,
	.power_put = z_ox08b_5300_9295_power_put,
	.set_mode = z_ox08b_5300_9295_set_mode,
	.start_streaming = z_ox08b_5300_9295_start_streaming,
	.stop_streaming = z_ox08b_5300_9295_stop_streaming,
};

static int z_ox08b_5300_9295_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);
	return 0;
}

static const struct v4l2_subdev_internal_ops z_ox08b_5300_9295_subdev_internal_ops = {
	.open = z_ox08b_5300_9295_open,
};

static int z_ox08b_5300_9295_board_setup(struct z_ox08b_5300_9295 *priv)
{
	struct device *dev = &priv->i2c_client->dev;
	struct device_node *node = dev->of_node;
	struct device_node *dser_node;
	struct i2c_client *dser_i2c = NULL;
	struct device_node *gmsl;
	int value = 0xFFFF;
	const char *str_value1[2], *str_value;
	int i;
	int err;

	err = of_property_read_u32(node, "reg", &priv->act_addr);
	if (err < 0) {
		dev_err(dev, "zuojisi: reg not found\n");
		goto error;
	}

	err = of_property_read_u32(node, "def-addr", &priv->def_addr);
	if (err < 0) {
		dev_err(dev, "zuojisi: def-addr not found\n");
		goto error;
	}

	priv->g_ctx.ser_dev = dev;

	dser_node = of_parse_phandle(node, "nvidia,gmsl-dser-device", 0);
	if (dser_node == NULL) {
		dev_err(dev, "zuojisi: missing %s handle\n", "nvidia,gmsl-dser-device");
		err = -EINVAL;
		goto error;
	}

	dser_i2c = of_find_i2c_device_by_node(dser_node);
	of_node_put(dser_node);

	if (dser_i2c == NULL) {
		dev_err(dev, "zuojisi: missing deserializer dev handle\n");
		err = -EINVAL;
		goto error;
	}
	if (dser_i2c->dev.driver == NULL) {
		dev_err(dev, "zuojisi: missing deserializer driver\n");
		err = -EINVAL;
		goto error;
	}

	priv->dser_dev = &dser_i2c->dev;
	priv->g_ctx.des_dev = &dser_i2c->dev;

	err = of_property_read_string(node, "des-link", &str_value);
	if (err < 0) {
		dev_err(dev, "zuojisi: des-link property is not found\n");
		return -EINVAL;
	}
	dev_info(dev, "zuojisi: link is %s\n", str_value);

	priv->des_link = str_value[0] - 'A';
	priv->g_ctx.des_link = priv->des_link;

	/* populate g_ctx from DT */
	gmsl = of_get_child_by_name(node, "gmsl-link");
	if (gmsl == NULL) {
		dev_err(dev, "zuojisi: missing gmsl-link device node\n");
		err = -EINVAL;
		goto error;
	}

	err = of_property_read_u32(gmsl, "num-ser-lanes", &value);
	if (err < 0) {
		dev_err(dev, "zuojisi: No num-lanes info\n");
		goto error;
	}
	priv->g_ctx.num_ser_csi_lanes = value;

	priv->g_ctx.num_streams =
			of_property_count_strings(gmsl, "streams");
	if (priv->g_ctx.num_streams <= 0) {
		dev_err(dev, "zuojisi: No streams found\n");
		err = -EINVAL;
		goto error;
	}

	for (i = 0; i < priv->g_ctx.num_streams; i++) {
		of_property_read_string_index(gmsl, "streams", i,
						&str_value1[i]);
		if (!str_value1[i]) {
			dev_err(dev, "zuojisi: invalid stream info\n");
			goto error;
		}
		if (!strcmp(str_value1[i], "raw12")) {
			priv->g_ctx.streams[i] = GMSL_CSI_DT_RAW_12;
		} else if (!strcmp(str_value1[i], "embed")) {
			priv->g_ctx.streams[i] = GMSL_CSI_DT_EMBED;
		} else if (!strcmp(str_value1[i], "ued-u1")) {
			priv->g_ctx.streams[i] = GMSL_CSI_DT_UED_U1;
		} else if (!strcmp(str_value1[i], "yuv16")) {
			priv->g_ctx.streams[i] = GMSL_CSI_DT_YUV16;
		} else {
			dev_err(dev, "zuojisi: invalid stream data type\n");
			goto error;
		}
	}

	priv->g_ctx.sensor_dev = dev;

	return 0;

error:
	dev_err(dev, "zuojisi: board setup failed\n");
	if (err == 0)
		err = -EINVAL;
	return err;
}

static int z_ox08b_5300_9295_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct tegracam_device *tc_dev;
	struct z_ox08b_5300_9295 *priv;
	int err;
	u8 val;

	dev_info(dev, "zuojisi: probing z_ox08b_5300_9295 sensor @0x%x.\n", client->addr);

	if (!IS_ENABLED(CONFIG_OF) || !node)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(struct z_ox08b_5300_9295), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "zuojisi: unable to allocate memory!\n");
		return -ENOMEM;
	}
	tc_dev = devm_kzalloc(dev, sizeof(struct tegracam_device), GFP_KERNEL);
	if (!tc_dev)
		return -ENOMEM;

	priv->i2c_client = tc_dev->client = client;
	tc_dev->dev = dev;
	strncpy(tc_dev->name, "z_ox08b_5300_9295", sizeof(tc_dev->name));
	tc_dev->dev_regmap_config = &sensor_regmap_config;
	tc_dev->sensor_ops = &z_ox08b_5300_9295_common_ops;
	tc_dev->v4l2sd_internal_ops = &z_ox08b_5300_9295_subdev_internal_ops;
	tc_dev->tcctrl_ops = &z_ox08b_5300_9295_ctrl_ops;

	err = z_ox08b_5300_9295_board_setup(priv);
	if (err) {
		dev_err(dev, "zuojisi: board setup failed\n");
		return err;
	}

	z_max96724_lock_link(priv->dser_dev);
	if (z_max96724_check_link_status(priv->dser_dev, priv->des_link)) {
		z_max96724_unlock_link(priv->dser_dev);
		dev_err(dev, "zuojisi: link_%c is occupied\n", 'A' + priv->des_link);
		return -EINVAL;
	}

	err = tegracam_device_register(tc_dev);
	if (err) {
		z_max96724_unlock_link(priv->dser_dev);
		dev_err(dev, "zuojisi: tegra camera driver registration failed\n");
		return err;
	}

	priv->tc_dev = tc_dev;
	priv->s_data = tc_dev->s_data;
	priv->subdev = &tc_dev->s_data->subdev;
	tegracam_set_privdata(tc_dev, (void *)priv);

	/* Monopolize the GMSL link and remap MAX9295A I2C address */
	z_max96724_monopolize_link(priv->dser_dev, priv->des_link);
	mdelay(50);

	/*
	 * Smart address remap: check if MAX9295A is already at remapped address
	 * (hot reboot case) before writing to default 0x40.
	 * This prevents writing to empty 0x40 when device is already remapped.
	 */
	err = iic_read(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x000d, &val);
	if (!err && val == 0x91) {
		dev_info(dev, "zuojisi: MAX9295A already at 0x%02x (hot reboot), skip remap\n",
			MAX9295_ALTER_ADDR_BASE + priv->des_link);
	} else {
		/* MAX9295A default address is 0x40 (7-bit), remap to 0x20+link */
		dev_info(dev, "zuojisi: remapping MAX9295A from 0x40 to 0x%02x\n",
			MAX9295_ALTER_ADDR_BASE + priv->des_link);
		iic_write(client, 0x40, 0x0000, (MAX9295_ALTER_ADDR_BASE + priv->des_link) << 1);
		mdelay(50);

		/* Verify MAX9295A is accessible after remap */
		err = iic_read(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x000d, &val);
		if (err || val != 0x91) {
			dev_err(dev, "zuojisi: access MAX9295A failed after remap (val=0x%02x)\n", val);
			z_max96724_restore_link(priv->dser_dev);
			tegracam_device_unregister(tc_dev);
			z_max96724_unlock_link(priv->dser_dev);
			return -EINVAL;
		}
	}

	/*
	 * Configure MAX9295A RCLK output via MFP0 for GW5300 ISP reset release.
	 * MFP0 also serves as GW5300 reset pin per module spec.
	 */
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x02bf, 0x60);
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x02be, 0x00);
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x02be, 0x10);
	mdelay(50);

	/*
	 * I2C address translation: remap GW5300 ISP address
	 * GW5300 original addr: 0x6D (7-bit, from 0xDA/8-bit)
	 * Host-side translated addr: act_addr (from DTS 'reg' property)
	 */
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x0044, priv->act_addr << 1); /* dst addr */
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x0045, priv->def_addr << 1); /* src addr (GW5300 0x6D) */

	/* Configure VC-ID mapping for video pipes */
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x007b, 0x30 + priv->des_link);
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x0083, 0x30 + priv->des_link);
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x0093, 0x30 + priv->des_link);
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x009b, 0x30 + priv->des_link);
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x00a3, 0x30 + priv->des_link);
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x00ab, 0x30 + priv->des_link);
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x008b, 0x30 + priv->des_link);

	/* Set CSI lane count on MAX9295A */
	iic_write(client, MAX9295_ALTER_ADDR_BASE + priv->des_link, 0x0331,
		  (((priv->g_ctx.num_ser_csi_lanes - 1) << 4)));

	z_max96724_enable_link(priv->dser_dev, priv->des_link);
	z_max96724_restore_link(priv->dser_dev);
	z_max96724_unlock_link(priv->dser_dev);

	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err) {
		dev_err(dev, "zuojisi: tegra camera subdev registration failed\n");
		return err;
	}

	dev_info(&client->dev, "zuojisi: Detected z_ox08b_5300_9295 (SG8-OX08BC-5300-GMSL2)\n");

	return 0;
}

static int z_ox08b_5300_9295_remove(struct i2c_client *client)
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct z_ox08b_5300_9295 *priv = (struct z_ox08b_5300_9295 *)s_data->priv;

	tegracam_v4l2subdev_unregister(priv->tc_dev);
	tegracam_device_unregister(priv->tc_dev);

	dev_info(&client->dev, "zuojisi: z_ox08b_5300_9295_remove sensor @0x%x.\n", client->addr);

	return 0;
}

static const struct i2c_device_id z_ox08b_5300_9295_id[] = {
	{ "z_ox08b_5300_9295", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, z_ox08b_5300_9295_id);

static struct i2c_driver z_ox08b_5300_9295_i2c_driver = {
	.driver = {
		.name = "z_ox08b_5300_9295",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(z_ox08b_5300_9295_of_match),
	},
	.probe = z_ox08b_5300_9295_probe,
	.remove = z_ox08b_5300_9295_remove,
	.id_table = z_ox08b_5300_9295_id,
};

static int __init z_ox08b_5300_9295_init(void)
{
	mutex_init(&serdes_lock__);
	return i2c_add_driver(&z_ox08b_5300_9295_i2c_driver);
}

static void __exit z_ox08b_5300_9295_exit(void)
{
	mutex_destroy(&serdes_lock__);
	i2c_del_driver(&z_ox08b_5300_9295_i2c_driver);
}

late_initcall(z_ox08b_5300_9295_init);
module_exit(z_ox08b_5300_9295_exit);

MODULE_DESCRIPTION("Media Controller driver for OX08B40+GW5300+MAX9295A (zuojisi MAX96724)");
MODULE_AUTHOR("zuojisi@163.com");
MODULE_LICENSE("GPL v2");
