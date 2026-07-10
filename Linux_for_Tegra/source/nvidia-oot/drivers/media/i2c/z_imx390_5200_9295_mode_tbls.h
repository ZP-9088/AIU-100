/* zuojisi@163.com */
#ifndef __Z_IMX390_5200_9295_I2C_TABLES__
#define __Z_IMX390_5200_9295_I2C_TABLES__

#include <media/camera_common.h>


enum {
	Z_IMX390_5200_9295_MODE_1920X1080_CROP_30FPS,
	Z_IMX390_5200_9295_MODE_START_STREAM,
	Z_IMX390_5200_9295_MODE_STOP_STREAM,
};

static const int imx390_30fps[] = {
	30,
};

static const struct camera_common_frmfmt z_imx390_5200_9295_frmfmt[] = {
	{{1920, 1536}, imx390_30fps, 1, 0, Z_IMX390_5200_9295_MODE_1920X1080_CROP_30FPS},
};
#endif /* __Z_IMX390_5200_9295_I2C_TABLES__ */
