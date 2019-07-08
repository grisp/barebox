/*
 * of_camera_selection.c - Selects Phytec Cameras
 *
 * Copyright (C) 2016 PHYTEC Messtechnik GmbH,
 * Author: Christian Hemp <c.hemp@phytec.de>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <common.h>
#include <filetype.h>
#include <libfile.h>
#include <of.h>
#include <command.h>
#include <malloc.h>
#include <complete.h>
#include <asm/byteorder.h>
#include <getopt.h>
#include <linux/err.h>
#include <string.h>
#include <linux/ctype.h>
#include <of_graph.h>


#define PHYCAM_P		"phyCAM-P"
#define PHYCAM_S		"phyCAM-S+"

#define PHYTEC_MEDIABUS		"/phytec_mediabus"
#define CAM_PORT_0		"/soc/ipu@02400000/port@0/endpoint/"
#define CAM_PORT_1		"/soc/ipu@02800000/port@1/endpoint/"

struct camera_info {
	int type;
	bool phycams;
	int i2c_address;
	int port;
};

struct phytec_camera {
	const char *name;
	const char *compatible;
};

static const struct phytec_camera phytec_cameras[] = {
	{
		.name = "VM-006",
		.compatible = "aptina,mt9m001",
	}, {
		.name = "VM-008",
		.compatible = "techwell,tw9910",
	}, {
		.name = "VM-009",
		.compatible = "micron,mt9m111",
	}, {
		.name = "VM-010-BW",
		.compatible = "aptina,mt9v024m",
	}, {
		.name = "VM-010-COL",
		.compatible = "aptina,mt9v024",
	}, {
		.name = "VM-011-BW",
		.compatible = "aptina,mt9p031m",
	}, {
		.name = "VM-011-COL",
		.compatible = "aptina,mt9p031",
	}, {
		.name = "VM-012-BW",
		.compatible = "phytec,vm012m",
	}, {
		.name = "VM-012-COL",
		.compatible = "phytec,vm012c",
	}
};

static int is_cam_supported(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(phytec_cameras); i++)
		if (!strcmp(name, phytec_cameras[i].name))
			return i;
	return -1;
}

static int get_camera_port(const struct device_node *node,
		struct device_node *root)
{
	struct device_node *port;

	if (!node || !root)
		return -1;

	port = of_graph_get_next_endpoint(node, NULL);
	if (!port) {
		pr_err("No endpoint for Camera found!\n");
		return -1;
	}

	port = of_graph_get_remote_port_from(port, root);
	if (!port) {
		pr_err("No remote endpoint for Camera found!\n");
		return -1;
	}

	if (strstr(CAM_PORT_0, port->full_name))
		return 0;
	else if (strstr(CAM_PORT_1, port->full_name))
		return 1;
	else
		return -1;
}

static struct device_node *find_camera(struct device_node *node,
				const char *name, int port)
{
	int camera = is_cam_supported(name);
	struct device_node *tmp_node;

	if (camera < 0)
		return NULL;

	for_each_compatible_node_from(tmp_node, node, NULL,
			phytec_cameras[camera].compatible) {

			if (port == get_camera_port(tmp_node, node))
				return tmp_node;
	}

	if (strstr(name, "-COL")) {
		char *tmp = xzalloc(strlen(name) + 1);

		strncpy(tmp, name, strlen(name) - strlen("-COL"));
		tmp = strcat(tmp, "-BW");

		camera = is_cam_supported(tmp);
		free(tmp);
		if (camera < 0)
			return NULL;

		for_each_compatible_node_from(tmp_node, node, NULL,
			phytec_cameras[camera].compatible) {

			if (port == get_camera_port(tmp_node, node))
				return tmp_node;
		}
	} else if (strstr(name, "-BW")) {
		char *tmp = xzalloc(strlen(name) + 2);

		strncpy(tmp, name, strlen(name) - strlen("-BW"));
		tmp = strcat(tmp, "-COL");

		camera = is_cam_supported(tmp);
		free(tmp);
		if (camera < 0)
			return NULL;

		for_each_compatible_node_from(tmp_node, node, NULL,
			phytec_cameras[camera].compatible) {
			if (port == get_camera_port(tmp_node, node))
				return tmp_node;
		}
	}

	return NULL;
}

static int of_set_phandle_to_remote_endpoint(struct device_node *cam,
				struct device_node *port)
{
	int ret;
	phandle p;

	p = of_node_create_phandle(cam);
	p = cpu_to_be32(p);

	ret = of_set_property(port, "remote-endpoint", &p, sizeof(p), 1);

	return ret;
}

static int set_camera_remote_endpoint(struct device_node *root,
		struct device_node *cam, struct camera_info *cam_info)
{
	struct device_node *port = NULL, *camendpoint = NULL;

	if (!root || !cam || !cam_info)
		return -EINVAL;

	if (cam_info->port == 0)
		port = of_find_node_by_path_from(root, CAM_PORT_0);
	else if (cam_info->port == 1)
		port = of_find_node_by_path_from(root, CAM_PORT_1);

	if (!port) {
		printf("Port not found\n");
		return -EINVAL;
	}

	camendpoint = of_graph_get_next_endpoint(cam, NULL);
	if (!camendpoint) {
		pr_err("Path to camera endpoint is not vaild.\n");
		return -EINVAL;
	}

	if (of_set_phandle_to_remote_endpoint(camendpoint, port) < 0) {
		pr_err("Could not set remote endpoint\n");
		return -EINVAL;
	}

	return 0;
}

static int change_camera(struct device_node *root, void *context)
{
	struct camera_info *cam_info = (struct camera_info *)context;
	struct device_node *cam = NULL, *mediabus = NULL;
	int ret = 0, i2c_address;

	cam = find_camera(root, phytec_cameras[cam_info->type].name,
							cam_info->port);
	if (!cam) {
		printf("Camera not found in dtb %s\n",
			phytec_cameras[cam_info->type].name);
		return -1;
	}

	ret = set_camera_remote_endpoint(root, cam, cam_info);
	if (ret < 0)
		return ret;

	if (cam_info->i2c_address > 0) {
		i2c_address = cpu_to_be32(cam_info->i2c_address);
		ret = of_set_property(cam, "reg", &i2c_address,
						sizeof(i2c_address), 1);
		if (ret < 0) {
			pr_err("Could not update property\n");
			return ret;
		}
	}

	ret = of_set_property(cam, "compatible",
			phytec_cameras[cam_info->type].compatible,
			strlen(phytec_cameras[cam_info->type].compatible) + 1, 1);
	if (ret < 0) {
		pr_err("Could not update compatible property\n");
		return ret;
	}

	if (cam_info->phycams) {
		char *type;

		mediabus = of_find_node_by_path_from(root, PHYTEC_MEDIABUS);
		if (!mediabus) {
			pr_err("Path to mediabus is not vaild.\n");
			return -EINVAL;
		}

		if (cam_info->port == 0)
			type = "phytec,cam0_serial";
		else if (cam_info->port == 1)
			type = "phytec,cam1_serial";
		else
			return -EINVAL;

		ret = of_set_property(mediabus, type, NULL, 0, 1);
		if (ret < 0) {
			pr_err("Could not set property\n");
			return ret;
		}
	}

	ret = of_device_enable(cam);

	return ret;
}

static int do_of_camera_selection(int argc, char *argv[])
{
	struct camera_info *cam_info = xzalloc(sizeof(struct camera_info));
	int list = 0, bus = 0, ret = 0;
	int opt;

	while ((opt = getopt(argc, argv, "a:b:lp:")) > 0) {
		switch (opt) {
		case 'a':
			cam_info->i2c_address = simple_strtoul(optarg, NULL, 0);
			break;
		case 'b':
			bus = 1;
			if (!strcmp(optarg, PHYCAM_P))
				cam_info->phycams = false;
			else if (!strcmp(optarg, PHYCAM_S))
				cam_info->phycams = true;
			else {
				printf("The bus typ is not supported %s\n",
					optarg);
				ret = COMMAND_ERROR_USAGE;
				goto free;
			}
			break;
		case 'p':
			cam_info->port = simple_strtoul(optarg, NULL, 0);
			break;
		case 'l':
			list = 1;
			break;
		default:
			ret = COMMAND_ERROR_USAGE;
			goto free;
		}
	}

	if (list) {
		int i;

		printf("CAMERA_NAME:\n");
		for (i = 0; i < ARRAY_SIZE(phytec_cameras); i++)
			printf("\t%s\n", phytec_cameras[i].name);

		goto free;
	}

	if (!bus) {
		ret = COMMAND_ERROR_USAGE;
		goto free;
	}

	if (optind == argc) {
		ret = COMMAND_ERROR_USAGE;
		goto free;
	}

	if (optind < argc) {
		cam_info->type = is_cam_supported(argv[optind]);
		if (cam_info->type < 0) {
			printf("Camera type not found %s\n", argv[optind]);
			goto free;
		};
	}

	of_register_fixup(change_camera, cam_info);

	return 0;
free:
	free(cam_info);
	return ret;
}

BAREBOX_CMD_HELP_START(of_camera_selection)
BAREBOX_CMD_HELP_TEXT("Options:")
BAREBOX_CMD_HELP_OPT("-a address",  "i2c address (HEX) of the camera.\n")
BAREBOX_CMD_HELP_OPT("-b",  "\tThe bus on which the camera is connected")
BAREBOX_CMD_HELP_TEXT("\tValid values for -b phyCAM-P or phyCAM-S+.\n")
BAREBOX_CMD_HELP_OPT("-l", "\tList Supportet values for CAMERA_NAME. Note: The other options have no effect\n")
BAREBOX_CMD_HELP_OPT("-p port",  "\ti.MX6 CSI port. Valid values for port are 0 or 1.\n")
BAREBOX_CMD_HELP_TEXT("Valid values for CAMERA_NAME are the camera order number without suffix '-LVDS.'")
BAREBOX_CMD_HELP_TEXT("Example for VM011 black/white on port 0 as phyCAM-P:")
BAREBOX_CMD_HELP_TEXT("\t of_camera_selection -a 0x48 -b phyCAM-P -p 0 VM-011-BW")
BAREBOX_CMD_HELP_TEXT("Example for VM011 Color on port 1 as phyCAM-S+:")
BAREBOX_CMD_HELP_TEXT("\t of_camera_selection -a 0x5d -b phyCAM-S+ -p 1 VM-011-COL")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(of_camera_selection)
	.cmd	= do_of_camera_selection,
	BAREBOX_CMD_DESC("Set the camera settings")
	BAREBOX_CMD_OPTS("[-a i2c address] -b bus [-l] [-p port] [-s] CAMERA_NAME")
	BAREBOX_CMD_GROUP(CMD_GRP_MISC)
	BAREBOX_CMD_COMPLETE(devicetree_file_complete)
	BAREBOX_CMD_HELP(cmd_of_camera_selection_help)
BAREBOX_CMD_END
