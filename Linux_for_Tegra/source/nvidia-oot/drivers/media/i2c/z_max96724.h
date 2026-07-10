/*
 * Copyright (c) 2018-2020, NVIDIA Corporation.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 * <b>max96724 API: For Maxim Integrated max96724 deserializer</b>
 *
 * @b Description: Defines elements used to set up and use a
 *  Maxim Integrated max96724 deserializer.
 */

#ifndef __max96724_H__
#define __max96724_H__

#include "z_gmsl-link.h"

int z_max96724_start_streaming(struct device *dev, struct gmsl_link_ctx *g_ctx);
int z_max96724_stop_streaming(struct device *dev, struct gmsl_link_ctx *g_ctx);

int z_max96724_check_link_status(struct device *dev, int link);
int z_max96724_monopolize_link(struct device *dev, int link);
int z_max96724_enable_link(struct device *dev, int link);
int z_max96724_restore_link(struct device *dev);
int z_max96724_check_link_status(struct device *dev, int link);
int z_max96724_set_link_bandwidth(struct device *dev, int link, int gbps);
int z_max96724_get_ox5b_config(struct device *dev, int link);
int z_max96724_lock_link(struct device *dev);
int z_max96724_unlock_link(struct device *dev);
int z_max96724_dump_video_status(struct device *dev, int link);

/** @} */

#endif  /* __max96724_H__ */
