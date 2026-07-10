/* zuojisi@163.com */
#ifndef __Z_OX08B_5300_9295_I2C_TABLES__
#define __Z_OX08B_5300_9295_I2C_TABLES__

#include <media/camera_common.h>

enum {
	Z_OX08B_5300_9295_MODE_3840X2160_30FPS,
};

static const int ox08b_30fps[] = {
	30,
};

static const struct camera_common_frmfmt z_ox08b_5300_9295_frmfmt[] = {
	{{3840, 2160}, ox08b_30fps, 1, 0, Z_OX08B_5300_9295_MODE_3840X2160_30FPS},
};

#endif /* __Z_OX08B_5300_9295_I2C_TABLES__ */
