/* SPDX-License-Identifier: GPL-2.0+ */
/* Marvell 10G 88x3310 PHY driver
 *
 * Author: Jackson Pooyappadam <jpooyappadam@marvell.com>
 */
#ifndef MARVELL10G_H
#define MARVELL10G_H

struct mv3310_priv {
	DECLARE_BITMAP(supported_interfaces, PHY_INTERFACE_MODE_MAX);
	const struct mv3310_mactype *mactype;

	u32 firmware_ver;
	bool has_downshift;

	bool rate_match;
	phy_interface_t const_interface;

	struct device *hwmon_dev;
	char *hwmon_name;
};

#endif /* MARVELL10G_H */
