/* zuojisi */

#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <media/camera_common.h>
#include <linux/module.h>
#include "z_max96724.h"

#define max96724_TX11_PIPE_X_EN_ADDR 0x90B
#define max96724_TX45_PIPE_X_DST_CTRL_ADDR 0x92D
#define max96724_PIPE_X_SRC_0_MAP_ADDR 0x90D


#define max96724_PHY_CLK (14)

struct max96724 {
	struct i2c_client *i2c_client;
	struct regmap *regmap;
	struct mutex lock;
	u8 link_lock_status;
	u8 ismaster;
	u8 link_initialized;  /* bitmap: tracks which links have been initialized */
	u8 mipi_mode;  /* 0: 1x4 mode (200W/300W), 1: 2x4 mode (800W) */
};

#define CHECKNULL(x) if(x==NULL){ \
		dev_err(NULL, "zuojisi %s: ##x## NULL ptr\n",__func__); \
		return -EINVAL; \
		}

static int check_device_id_rev(struct i2c_client *client);
// static int check_link(struct i2c_client *client);

static int max96724_write_reg(struct device *dev,
	u16 addr, u8 val)
{
	struct max96724 *priv;
	int err;

	priv = dev_get_drvdata(dev);

	err = regmap_write(priv->regmap, addr, val);
	if (err)
		dev_err(dev,
		"zuojisi %s:i2c write failed, 0x%x = %x\n",
		__func__, addr, val);

	/* delay before next i2c command as required for SERDES link */
	usleep_range(100, 110);
	if(!err)
		dev_info(dev,"zuojisi:96724:w addr(0x%04x)->0x%02x\n",addr,val);

	return err;
}

static int max96724_read_reg(struct device *dev,
	u16 addr, u32* val)
{
	struct max96724 *priv;
	int err;

	priv = dev_get_drvdata(dev);

	err = regmap_read(priv->regmap, addr, val);
	if (err)
		dev_err(dev,
		"zuojisi %s:i2c read failed, 0x%x\n",
		__func__, addr);

	/* delay before next i2c command as required for SERDES link */
	usleep_range(100, 110);
	if(!err)
		dev_info(dev,"zuojisi:96724:r addr(0x%04x)<-0x%02x\n",addr,(u8)(*val));
	return err;
}

int z_max96724_lock_link(struct device *dev)
{
	struct max96724 *priv = NULL;

	CHECKNULL(dev);
	priv = dev_get_drvdata(dev);
	CHECKNULL(priv);

	mutex_lock(&priv->lock);
	dev_info(dev,"zuojisi: mutex lock");
	return 0;
}
EXPORT_SYMBOL(z_max96724_lock_link);

int z_max96724_unlock_link(struct device *dev)
{
	struct max96724 *priv = NULL;

	CHECKNULL(dev);
	priv = dev_get_drvdata(dev);
	CHECKNULL(priv);

	dev_info(dev,"zuojisi: mutex unlock");
	mutex_unlock(&priv->lock);
	return 0;
}
EXPORT_SYMBOL(z_max96724_unlock_link);

int z_max96724_monopolize_link(struct device *dev, int link)
{
	struct max96724 *priv = NULL;
	u32 value;

	CHECKNULL(dev);
	priv = dev_get_drvdata(dev);
	CHECKNULL(priv);

	dev_info(dev, "zuojisi: %s: monopolize link_%c\n", __func__, 'A'+link);
	max96724_read_reg(dev,0x0006,&value);
	value = (value & 0xF0) | (1<<link);
	max96724_write_reg(dev,0x0006,value);
	mdelay(150);
	return 0;
}
EXPORT_SYMBOL(z_max96724_monopolize_link);

int z_max96724_enable_link(struct device *dev, int link)
{
    struct max96724 *priv = NULL;
 
    CHECKNULL(dev);
    priv = dev_get_drvdata(dev);
    CHECKNULL(priv);
 
    dev_info(dev, "zuojisi: %s: enable link_%c\n", __func__, 'A'+link);
    priv->link_lock_status |= (1<<link);
    return 0;
}
EXPORT_SYMBOL(z_max96724_enable_link);

int z_max96724_restore_link(struct device *dev)
{
	struct max96724 *priv = NULL;
	u32 value;

	CHECKNULL(dev);
	priv = dev_get_drvdata(dev);
	CHECKNULL(priv);

	dev_info(dev, "zuojisi: restore links\n");
	max96724_read_reg(dev,0x0006,&value);
	max96724_write_reg(dev,0x0006,(value&0xF0)|(priv->link_lock_status&0x0F));
	mdelay(150);
	return 0;
}
EXPORT_SYMBOL(z_max96724_restore_link);

int z_max96724_check_link_status(struct device *dev, int link)
{
	struct max96724 *priv = NULL;
	CHECKNULL(dev);
	priv = dev_get_drvdata(dev);
	CHECKNULL(priv);
	if(priv->link_lock_status & (0x1<<link)){
		return 1;
	}else{
		return 0;
	}
}
EXPORT_SYMBOL(z_max96724_check_link_status);

int z_max96724_set_link_bandwidth(struct device *dev, int link, int gbps)
{
	struct max96724 *priv = NULL;
	u32 value;
	u16 reg = 0x0010;
	u8 regval = 0x01;
	if(gbps!=3){ //6Gbps
		regval = 0x02;
	}

	CHECKNULL(dev);
	priv = dev_get_drvdata(dev);
	CHECKNULL(priv);

	dev_info(dev, "zuojisi: set link_%c to %dGbps\n",link+'A',gbps);
	if(link>=2){
		reg++, link-=2;
	}
	max96724_read_reg(dev,reg,&value);
	value = (value & (~(0x3<<(link*4)))) | (regval<<(link*4));
	max96724_write_reg(dev,reg,value & 0xFF);
	max96724_write_reg(dev,0x0018,(1<<link));
	mdelay(300);
	return 0;
}
EXPORT_SYMBOL(z_max96724_set_link_bandwidth);

static int max96724_setup_pipeline(struct device *dev,struct gmsl_link_ctx *g_ctx)
{
	struct max96724 *priv = NULL;
	int pipe_id = 0;
	u32 i = 0;
	u8 dst_vc = 0,src_vc = 0;
	u8 dst_ctrl = 0;

	CHECKNULL(dev);
	CHECKNULL(g_ctx);
	priv = dev_get_drvdata(dev);
	CHECKNULL(priv);

	dev_info(dev,"zuojisi:96724: num_streams=%d, mipi_mode=%s\n",
		g_ctx->num_streams, priv->mipi_mode ? "2x4" : "1x4");

	for (i = 0; i < g_ctx->num_streams; i++) {
		pipe_id = g_ctx->des_link;
		dev_info(dev, "zuojisi: pipe_%d receive stream_id=%d, datatype=0x%x\n", pipe_id,i,g_ctx->streams[i]);

		if (priv->mipi_mode) {
			/* 2x4 mode: pipe0,1→VC0/1 on Port A, pipe2,3→VC0/1 on Port B */
			dst_vc = g_ctx->des_link & 0x01;  /* pipe0→VC0, pipe1→VC1, pipe2→VC0, pipe3→VC1 */
			/* 2x4 mode: pipe0,1→Controller 1 (Port A), pipe2,3→Controller 2 (Port B) */
			dst_ctrl = (pipe_id < 2) ? 0x55 : 0xAA;
		} else {
			/* 1x4 mode: all pipes→VC0-3 on single port (Controller 2) */
			dst_vc = g_ctx->des_link;  /* pipe0→VC0, pipe1→VC1, pipe2→VC2, pipe3→VC3 */
			/* 1x4 mode: all pipes→Controller 2 (Port B) */
			dst_ctrl = 0xAA;
		}

		max96724_write_reg(dev, (0x40*pipe_id)+max96724_TX11_PIPE_X_EN_ADDR+0, 0x1f);
		max96724_write_reg(dev, (0x40*pipe_id)+max96724_TX45_PIPE_X_DST_CTRL_ADDR+0, dst_ctrl);
        max96724_write_reg(dev, (0x40*pipe_id)+max96724_TX45_PIPE_X_DST_CTRL_ADDR+1, 0x02); 
		max96724_write_reg(dev, (0x40*pipe_id)+max96724_PIPE_X_SRC_0_MAP_ADDR+0, g_ctx->streams[i] | (src_vc << 6));
		max96724_write_reg(dev, (0x40*pipe_id)+max96724_PIPE_X_SRC_0_MAP_ADDR+1, g_ctx->streams[i] | (dst_vc << 6));
		max96724_write_reg(dev, (0x40*pipe_id)+max96724_PIPE_X_SRC_0_MAP_ADDR+2, 0x00 | (src_vc << 6));
		max96724_write_reg(dev, (0x40*pipe_id)+max96724_PIPE_X_SRC_0_MAP_ADDR+3, 0x00 | (dst_vc << 6));
		max96724_write_reg(dev, (0x40*pipe_id)+max96724_PIPE_X_SRC_0_MAP_ADDR+4, 0x01 | (src_vc << 6));
		max96724_write_reg(dev, (0x40*pipe_id)+max96724_PIPE_X_SRC_0_MAP_ADDR+5, 0x01 | (dst_vc << 6));
		max96724_write_reg(dev, (0x40*pipe_id)+max96724_PIPE_X_SRC_0_MAP_ADDR+6, 0x02 | (src_vc << 6));
		max96724_write_reg(dev, (0x40*pipe_id)+max96724_PIPE_X_SRC_0_MAP_ADDR+7, 0x02 | (dst_vc << 6));
		max96724_write_reg(dev, (0x40*pipe_id)+max96724_PIPE_X_SRC_0_MAP_ADDR+8, 0x03 | (src_vc << 6));
		max96724_write_reg(dev, (0x40*pipe_id)+max96724_PIPE_X_SRC_0_MAP_ADDR+9, 0x03 | (dst_vc << 6));			
	}

	return 0;
}

int z_max96724_start_streaming(struct device *dev, struct gmsl_link_ctx *g_ctx)
{
	struct max96724 *priv = NULL;
	int err;
	u32 val;
	
	CHECKNULL(dev);
	CHECKNULL(g_ctx);
	priv = dev_get_drvdata(dev);
	CHECKNULL(priv);

	dev_info(dev,"zuojisi: 96724 start streaming, link=%c - enter\n",'A'+g_ctx->des_link);

	mutex_lock(&priv->lock);
	err = max96724_setup_pipeline(dev, g_ctx);
	if (err)
		return err;

	max96724_read_reg(dev,0xf4,&val);

	/*
	 * Only reset link and PHYs on first start for this link.
	 * For hot restart, skip link reset to avoid GMSL renegotiation.
	 */
	if ((priv->link_initialized & (1<<g_ctx->des_link)) == 0) {
		/* First start: enable pipe and reset PHY/link */
		dev_info(dev,"zuojisi: 96724 first start, init link_%c\n",'A'+g_ctx->des_link);
		max96724_write_reg(dev,0xf4, (val | (1<<g_ctx->des_link))&0xff );

		// reset mipi phy: toggle relevant PHYs then enable all
		if(g_ctx->des_link==0 || g_ctx->des_link==1){
			max96724_write_reg(dev,0x8a2, 0xc0); // keep PHY2+3, reset PHY0+1
		}else{
			max96724_write_reg(dev,0x8a2, 0x30); // keep PHY0+1, reset PHY2+3
		}
		mdelay(10);
		max96724_write_reg(dev,0x8a2, 0xf0); // enable all PHYs
		mdelay(10);

		// reset link
		max96724_write_reg(dev,0x018, (1<<g_ctx->des_link));
		mdelay(150);

		/* Mark this link as initialized */
		priv->link_initialized |= (1<<g_ctx->des_link);
	} else {
		/* Hot restart: link already initialized, just re-enable pipe */
		dev_info(dev,"zuojisi: 96724 hot restart, skip link reset\n");
		max96724_write_reg(dev,0xf4, (val | (1<<g_ctx->des_link))&0xff );
	}

	//skew calibration for all 4 PHYs
	max96724_write_reg(dev,0x0903,0x10); // PHY0 skew reset
	max96724_write_reg(dev,0x0903,0x33); // PHY0 skew enable
	max96724_write_reg(dev,0x0943,0x10); // PHY1 skew reset
	max96724_write_reg(dev,0x0943,0x33); // PHY1 skew enable
	max96724_write_reg(dev,0x0983,0x10); // PHY2 skew reset
	max96724_write_reg(dev,0x0983,0x33); // PHY2 skew enable
	max96724_write_reg(dev,0x09c3,0x10); // PHY3 skew reset
	max96724_write_reg(dev,0x09c3,0x33); // PHY3 skew enable

	mutex_unlock(&priv->lock);
	dev_info(dev,"zuojisi: 96724 start streaming - exit\n");
	return 0;
}
EXPORT_SYMBOL(z_max96724_start_streaming);

int z_max96724_stop_streaming(struct device *dev, struct gmsl_link_ctx *g_ctx)
{
	struct max96724 *priv = NULL;
	u32 val;
	u8 tmp;

	CHECKNULL(dev);
	CHECKNULL(g_ctx);
	priv = dev_get_drvdata(dev);
	CHECKNULL(priv);

	dev_info(dev,"zuojisi: 96724 stop streaming, link_%c - enter\n",'A'+g_ctx->des_link);
	mutex_lock(&priv->lock);

	/* Only disable the video pipe, don't reset the GMSL link */
	max96724_read_reg(dev,0xf4,&val);
	tmp = (val & (~(1<<g_ctx->des_link))) & 0xff;
	max96724_write_reg(dev,0xf4, tmp);

	/*
	 * Do NOT reset links when all pipes are stopped.
	 * The GMSL link should remain active for quick restart.
	 * Writing 0x018 = 0x0f would cause link renegotiation.
	 */
	if(tmp==0){
		dev_info(dev,"zuojisi: all pipes disabled, keeping links active\n");
	}

	mutex_unlock(&priv->lock);
	dev_info(dev,"zuojisi: 96724 stop streaming - exit\n");
	return 0;
}
EXPORT_SYMBOL(z_max96724_stop_streaming);

const struct of_device_id max96724_of_match[] = {
	{ .compatible = "zuojisi,z_max96724", },
	{ },
};
MODULE_DEVICE_TABLE(of, max96724_of_match);

static int max96724_parse_dt(struct max96724 *priv,
				struct i2c_client *client)
{
	struct device_node *node = client->dev.of_node;
	const struct of_device_id *match;

	if (!node)
		return -EINVAL;

	match = of_match_device(max96724_of_match, &client->dev);
	if (!match) {
		dev_err(&client->dev, "Failed to find matching dt id\n");
		return -EFAULT;
	}

	return 0;
}

static struct regmap_config max96724_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
};

static int get_mipifreq(void)
{
	struct file *file;
	loff_t pos = 0;
	char buf[10];
	int freq = 0;
	ssize_t ret;

	file = filp_open("/etc/mipifreq", O_RDONLY, 0);
	if (IS_ERR(file)) {
		printk("zuojisi: no mipifreq define file\n");
		goto defaultval;
	}

	ret = kernel_read(file, buf, 4, &pos);
	filp_close(file, NULL);
	if (ret > 0) {
		buf[ret] = 0;
		printk("zuojisi: read from mipifreq: %s\n", buf);
		freq = simple_strtoul(buf,NULL,10);
		freq = freq / 100;
		if(freq<2 || freq>25){
			printk("zuojisi: mipifreq out of range: %d, use default\n", freq);
			goto defaultval;
		}
		return freq;
	} else {
		printk("zuojisi: failed to read from mipifreq\n");
		goto defaultval;
	}

defaultval:
	return (max96724_PHY_CLK&0x1f);
}

static int max96724_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct max96724 *priv;
	int err = 0;
	u32 val = 0;
	struct device_node *node = client->dev.of_node;
	int mipifreq = 0;

	dev_info(&client->dev, "zuojisi: [max96724]: probing GMSL Deserializer @0x%x\n",client->addr);

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	priv->i2c_client = client;
	priv->regmap = devm_regmap_init_i2c(priv->i2c_client,
				&max96724_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(priv->regmap));
		return -ENODEV;
	}

	err = max96724_parse_dt(priv, client);
	if (err) {
		dev_err(&client->dev, "zuojisi: unable to parse dt\n");
		return -EFAULT;
	}

	mutex_init(&priv->lock);

	dev_set_drvdata(&client->dev, priv);

	err=check_device_id_rev(client);
	if(err){
		dev_err(&client->dev, "zuojisi: failed on check id & rev\n");
		return -EFAULT;
	}

	if (of_get_property(node, "is-master", NULL)) {
		priv->ismaster = 1;
		dev_info(&client->dev, "zuojisi: this 96724 is master\n");
	}else{
		priv->ismaster = 0;
		dev_info(&client->dev, "zuojisi: this 96724 is slave\n");
	}

	/* Parse MIPI mode from device tree */
	if (of_property_read_bool(node, "mipi-2x4-mode")) {
		priv->mipi_mode = 1;  /* 2x4 mode for 800W cameras */
		dev_info(&client->dev, "zuojisi: MIPI 2x4 mode (800W)\n");
	} else {
		priv->mipi_mode = 0;  /* 1x4 mode for 200W/300W cameras */
		dev_info(&client->dev, "zuojisi: MIPI 1x4 mode (200W/300W)\n");
	}

	err=max96724_write_reg(&client->dev,0x0013,0x40); //reset all
	mdelay(100);
	err=max96724_write_reg(&client->dev,0x0013,0x00);

#if 1
	err=max96724_write_reg(&client->dev,0x0005,0x88);
	err=max96724_write_reg(&client->dev,0x0310,0x90); 
#else
	err=max96724_write_reg(&client->dev,0x0005,0xc8);
#endif

	//turn on LED
	err=max96724_write_reg(&client->dev,0x0309,0x90);

	//disable Links which are not Locked
	err=max96724_read_reg(&client->dev,0x0006,&val);
	err=max96724_write_reg(&client->dev,0x0006,(val&0xF0)|(priv->link_lock_status&0x0F));


	max96724_write_reg(&client->dev,0x90a,0xc0); //4 lanes, D-Phy, 2bits VC  /* Modified: was 0x40(2 lanes) */
	max96724_write_reg(&client->dev,0x94a,0xc0); //4 lanes, D-Phy, 2bits VC  /* Modified: was 0x40(2 lanes) */
	max96724_write_reg(&client->dev,0x98a,0xc0); //4 lanes, D-Phy, 2bits VC  /* Modified: was 0x40(2 lanes) */
	max96724_write_reg(&client->dev,0x9ca,0xc0); //4 lanes, D-Phy, 2bits VC  /* Modified: was 0x40(2 lanes) */

	max96724_write_reg(&client->dev,0x8a3,0xe4); //phy lane mapping
	max96724_write_reg(&client->dev,0x8a4,0xe4); //phy lane mapping

	/* Configure MIPI PHY mode based on camera type */
	if (priv->mipi_mode) {
		max96724_write_reg(&client->dev,0x8a0,0x24); // 2x4 mode: Port A=PHY0+1, Port B=PHY2+3 (800W)
	} else {
		max96724_write_reg(&client->dev,0x8a0,0x04); // 1x4 mode: single 4-lane port (200W/300W)
	}
	max96724_write_reg(&client->dev,0x8a2,0xf0); // enable all 4 MIPI TX PHYs (PHY0-3)

	mipifreq = get_mipifreq();
	max96724_write_reg(&client->dev,0x415,0x20|mipifreq);
	max96724_write_reg(&client->dev,0x418,0x20|mipifreq);
	max96724_write_reg(&client->dev,0x41b,0x20|mipifreq);
	max96724_write_reg(&client->dev,0x41e,0x20|mipifreq);

	max96724_write_reg(&client->dev,0xf0,0x62); //pipe1 GMSLB, Z-pipe; pipe0 GMSLA, Z-pipe
	max96724_write_reg(&client->dev,0xf1,0xea); //pipe3 GMSLD, Z-pipe; pipe2 GMSLC, Z-pipe
	max96724_write_reg(&client->dev,0xf4,0x00); //disable pipes 0-4 (default val)

	/* Frame sync GPIO TX disabled - cameras run free-running */

	max96724_write_reg(&client->dev,0x0027,0x00); //disable unconcerned error report
	max96724_write_reg(&client->dev,0x0029,0xf0); //disable unconcerned error report
	// max96724_write_reg(&client->dev,0x1250,0xff); //set 128 ECC threshold, reset error cnt

	/* dev communication gets validated when GMSL link setup is done */
	dev_info(&client->dev, "zuojisi: %s:  success\n", __func__);

	return err;
}

static int check_device_id_rev(struct i2c_client *client)
{
	int err=0;
	unsigned int value=0;

	err=max96724_read_reg(&client->dev,0x004C,&value);
	dev_info(&client->dev,"zuojisi:device revision=0x%x\n",(value&0x0f));
	return err;
}

static int max96724_remove(struct i2c_client *client)
{
	struct max96724 *priv;

	if (client != NULL) {
		priv = dev_get_drvdata(&client->dev);
		mutex_destroy(&priv->lock);
		i2c_unregister_device(client);
		client = NULL;
	}

	return 0;
}

static const struct i2c_device_id max96724_id[] = {
	{ "max96724", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, max96724_id);

static struct i2c_driver max96724_i2c_driver = {
	.driver = {
		.name = "z_max96724",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(max96724_of_match),
	},
	.probe = max96724_probe,
	.remove = max96724_remove,
	.id_table = max96724_id,
};

static int __init max96724_init(void)
{
	return i2c_add_driver(&max96724_i2c_driver);
}

static void __exit max96724_exit(void)
{
	i2c_del_driver(&max96724_i2c_driver);
}

module_init(max96724_init);
module_exit(max96724_exit);

MODULE_DESCRIPTION("GMSL Deserializer driver z_max96724");
MODULE_AUTHOR("zuojisi zuojisi@163.com");
MODULE_LICENSE("GPL v2");

