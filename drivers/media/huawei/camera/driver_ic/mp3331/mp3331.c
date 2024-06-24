/*
 *  Hisilicon K3 SOC camera driver source file
 *
 *  Copyright (C) Huawei Technology Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/videodev2.h>
#include <media/huawei/camera.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>

#include "hwcam_intf.h"
#include "../hwdriver_ic.h"
#include "cam_log.h"

#define INTF2DRIVERIC(i) container_of(i, driveric_t, intf)

#define DRIVER_IC_ENABLE_PIN (10)
#define BUCKBOOST_ENABLE_PIN (8)
#define INVALID_GPIO        (999)
#define INVALID_I2C_INDEX   (-1)
#define INVALID_IC_POSITION (-1)
#define MP3331_GPIO_ENABLE  (1)
#define MP3331_GPIO_DISABLE (0)

#define DRIVER_IC_ENABLE_PIN (10)
#define BUCKBOOST_ENABLE_PIN (8)

#define RETURN_ERROR_IF(x) \
    if ((x)) { \
    cam_err("%s error %s.", __func__, #x); \
    return -EINVAL; \
    }

#define RETURN_ERROR_ON_NULL(x) \
    if ((x)==NULL) { \
    cam_err("%s invalid params %s.", __func__, #x); \
    return -EINVAL; \
    }

typedef enum {
    POWER_ENABLE = 0,
    MP3331_MAX_GPIOPIN,
}drv_ic_mp3331_pin_type_t;

typedef enum {
    FREED = 0,
    REQUESTED,
}drv_ic_mp3331_pin_status_t;

typedef enum{
    DISABLE = 0,
    ENABLE = 1,
}powertype_t;

typedef struct _drv_ic_mp3331_pin_t {
    unsigned int pin_id;
    drv_ic_mp3331_pin_status_t pin_status;
}drv_ic_mp3331_pin_t;

typedef struct _driver_ic_mp3331_private_data_t {
    drv_ic_mp3331_pin_t pin[MP3331_MAX_GPIOPIN];
}drv_ic_mp3331_private_data_t;

static driveric_t s_mp3331;
static struct platform_device *s_pdev = NULL;
static drv_ic_mp3331_private_data_t s_mp3331_pdata;

void mp3331_notify_error(uint32_t id);//stub

static int mp3331_get_dt_data(const hwdriveric_intf_t *intf, struct device_node *dev_node)
{
    driveric_t *drv_ic = NULL;
    drv_ic_mp3331_private_data_t *pdata = NULL;
    unsigned int pin_tmp[MP3331_MAX_GPIOPIN] = {0};
    int rc = 0;
    int i  = 0;
    unsigned int position, i2c_index;

    RETURN_ERROR_ON_NULL(intf);
    RETURN_ERROR_ON_NULL(dev_node);

    drv_ic = INTF2DRIVERIC(intf);
    RETURN_ERROR_ON_NULL(drv_ic);
    RETURN_ERROR_ON_NULL(drv_ic->pdata);

    pdata = (drv_ic_mp3331_private_data_t *)drv_ic->pdata;

    rc = of_property_read_u32(dev_node, "huawei,position", (u32 *)&position);
    if (rc < 0) {
        cam_err("%s get dt position failed", __func__);
        drv_ic->position = INVALID_IC_POSITION;
        goto mp3331_get_dt_error_handler;
    } else {
        drv_ic->position = position;
        cam_info("%s position(%u)", __func__, drv_ic->position);
    }

    rc = of_property_read_u32(dev_node, "huawei,i2c_index", (u32 *)&i2c_index);
    if (rc < 0) {
        cam_err("%s get dt i2c_index failed", __func__);
        drv_ic->i2c_index = INVALID_I2C_INDEX;
        goto mp3331_get_dt_error_handler;
    } else {
        drv_ic->i2c_index = i2c_index;
        cam_info("%s i2c_index(%u)", __func__, drv_ic->i2c_index);
    }

    rc = of_property_read_u32_array(dev_node, "huawei,gpio_pin",
                                    pin_tmp, MP3331_MAX_GPIOPIN);
    if (rc < 0) {
        cam_err("%s get dt gpio-pin failed", __func__);
        for (i = 0; i < MP3331_MAX_GPIOPIN; i++) {
            pdata->pin[i].pin_id = INVALID_GPIO;
            pdata->pin[i].pin_status = FREED;
        }
        goto mp3331_get_dt_error_handler;
    } else {
        for (i = 0; i < MP3331_MAX_GPIOPIN; i++) {
            pdata->pin[i].pin_id = pin_tmp[i];
            pdata->pin[i].pin_status = FREED;
            cam_info("%s gpio-pin[%d] = %u.", __func__, i, pdata->pin[i].pin_id);
        }
    }

    return rc;

mp3331_get_dt_error_handler:

    return rc;
}



static int drv_ic_mp3331_set_pin(drv_ic_mp3331_private_data_t *pdata, int state)
{
    int rc = 0;
    int i=0;

    RETURN_ERROR_ON_NULL(pdata);

    for(i=0;i<MP3331_MAX_GPIOPIN;i++)
    {
        cam_debug("%s pin_id=%d, state=%d.", __func__, pdata->pin[i].pin_id, state);

        if(INVALID_GPIO == pdata->pin[i].pin_id)
        {
            cam_err("%s gpio pin is err rc=%d.", __func__, pdata->pin[i].pin_id);
            return -1;
        }
        rc = gpio_request(pdata->pin[i].pin_id, NULL);
        if (rc < 0) {
            cam_err("%s, gpio-request %d", __func__, pdata->pin[i].pin_id);
            return -1;
        }

        pdata->pin[i].pin_status = REQUESTED;
        rc = gpio_direction_output(pdata->pin[i].pin_id, state);
        if (rc < 0) {
            cam_err("%s gpio output is err rc=%d.", __func__, rc);
        }

        gpio_free(pdata->pin[i].pin_id);
    }

    return rc;
}


int mp3331_power_on(const hwdriveric_intf_t* i)
{
    int rc = 0;
    driveric_t *drv_ic = NULL;
    drv_ic_mp3331_private_data_t *pdata = NULL;
    cam_info("%s power on enter", __func__);
    drv_ic = INTF2DRIVERIC(i);
    RETURN_ERROR_ON_NULL(drv_ic);
    RETURN_ERROR_ON_NULL(drv_ic->pdata);

    pdata = (drv_ic_mp3331_private_data_t *)drv_ic->pdata;

    rc = drv_ic_mp3331_set_pin( pdata, ENABLE);

    return rc;
}

int mp3331_power_off(const hwdriveric_intf_t* i)
{
    int rc = 0;

    driveric_t *drv_ic = NULL;
    drv_ic_mp3331_private_data_t *pdata = NULL;
    cam_info("%s power off enter", __func__);

    drv_ic = INTF2DRIVERIC(i);
    RETURN_ERROR_ON_NULL(drv_ic);
    RETURN_ERROR_ON_NULL(drv_ic->pdata);

    pdata = (drv_ic_mp3331_private_data_t *)drv_ic->pdata;

    rc = drv_ic_mp3331_set_pin( pdata, DISABLE);

    return rc;
}

char const* mp3331_get_name(const hwdriveric_intf_t* i)
{
    driveric_t* mp3331 = NULL;
    mp3331 = INTF2DRIVERIC(i);
    return mp3331->name;
}

void mp3331_notify_error( uint32_t id)
{
	hwdriveric_event_t mp3331_ev;
	mp3331_ev.kind = HWDRIVERIC_INFO_ERROR;
	mp3331_ev.data.error.id = id;
	cam_info("id = %x", id);
	hwdriveric_intf_notify_error(s_mp3331.notify, &mp3331_ev);
}

static int mp3331_init(const hwdriveric_intf_t *intf)
{
    cam_info("mp3331 init enter");
    cam_info("%s init success", __func__);
    return 0;
}

static hwdriveric_vtbl_t
s_vtbl_mp3331 =
{
    .get_name = mp3331_get_name,
    .power_on = mp3331_power_on,
    .power_off = mp3331_power_off,
    .init = mp3331_init,
    .driveric_get_dt_data = mp3331_get_dt_data,
};

static driveric_t
s_mp3331 =
{
    .name = "drv_ic_mp3331",
    .intf = { .vtbl = &s_vtbl_mp3331, },
    .pdata = (void*)&s_mp3331_pdata,
};

static const struct of_device_id
s_mp3331_dt_match[] =
{
	{
        .compatible = "huawei,drv_ic_mp3331",
        .data = &s_mp3331.intf,
    },
	{
    },
};

MODULE_DEVICE_TABLE(of, s_driveric_dt_match);

static struct platform_driver
s_mp3331_driver =
{
	.driver =
    {
		.name = "huawei,drv_ic_mp3331",
		.owner = THIS_MODULE,
		.of_match_table = s_mp3331_dt_match,
	},
};

static int32_t
mp3331_platform_probe(
        struct platform_device* pdev)
{
    cam_notice("%s enter", __func__);
    s_pdev = pdev;
    return hwdriveric_register(pdev, &s_mp3331.intf, &s_mp3331.notify);
}

static int __init
mp3331_init_module(void)
{
	int ret = 0;

	cam_notice("%s enter", __func__);

	ret = platform_driver_probe(&s_mp3331_driver,
			mp3331_platform_probe);

	return ret;
}

static void __exit
mp3331_exit_module(void)
{
    platform_driver_unregister(&s_mp3331_driver);
    hwdriveric_unregister(s_pdev);
}

module_init(mp3331_init_module);
module_exit(mp3331_exit_module);
MODULE_DESCRIPTION("mp3331");
MODULE_LICENSE("GPL v2");

