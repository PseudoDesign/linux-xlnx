/*
 * A hwmon driver for the Linear Technology LTC2946
 * Copyright (C) 2019 Pseudo Design, LLC.
 *
 * Author: Adam Schafer <adam@pseudo.design>
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

#include <linux/module.h>   /* Needed by all modules */
#include <linux/init.h>     /* Needed for the macros */
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/util_macros.h>
#include <linux/i2c.h>
#include <linux/printk.h>

#define REG_POWER_MAX			0x08
#define REG_POWER_MIN			0x0B
#define REG_POWER			0x05

#define POWER_VALUE_TO_NWATT		31250

struct ltc2946_data {
        struct i2c_client *client;
};

/* Functions supporting the i2c transactions */
static unsigned int read_uint24(struct i2c_client *client, u8 address)
{
	u8 bytes[3];

	i2c_smbus_read_i2c_block_data(client, address, 3, bytes);

	return (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
}

static int write_uint24(struct i2c_client *client, u8 address, unsigned int value)
{
	unsigned int input = clamp_val(value, 0, 0xFFFFFF);

	u8 bytes[] = {
		0xFF & (input >> 16),
		0xFF & (input >> 8),
		0xFF & input,
	};

	return i2c_smbus_write_i2c_block_data(client, address, 3, bytes);
}

/* Functions supporting the sensor attributes */

/* Power Attributes */

static ssize_t show_power_value(struct device *dev, u8 address, struct device_attribute *devattr, char *buf)
{
	struct ltc2946_data *data = dev_get_drvdata(dev);
        unsigned long output = read_uint24(data->client, address) * POWER_VALUE_TO_NWATT;

        return sprintf(buf, "%ld\n", output / 1000);
}

static ssize_t set_power_value(struct device *dev, u8 address, struct device_attribute *devattr, const char *buf, size_t count)
{
	long input;
        int retval;
        struct ltc2946_data *data = dev_get_drvdata(dev);

	if (kstrtol(buf, count, &input))
		return -EINVAL;

	input *= 1000;
        input /= POWER_VALUE_TO_NWATT;
        
	retval = write_uint24(data->client, address, (unsigned int)input);

        if (retval < 0)
                return retval;

        return count;
}

static ssize_t show_power_max(struct device *dev, struct device_attribute *devattr, char *buf)
{
	return show_power_value(dev, REG_POWER_MAX, devattr, buf);
}

static ssize_t set_power_max(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	return set_power_value(dev, REG_POWER_MAX, devattr, buf, count);
}

static ssize_t show_power_min(struct device *dev, struct device_attribute *devattr, char *buf)
{
	return show_power_value(dev, REG_POWER_MIN, devattr, buf);
}

static ssize_t set_power_min(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	return set_power_value(dev, REG_POWER_MIN, devattr, buf, count);
}

static ssize_t show_power_input(struct device *dev, struct device_attribute *devattr, char *buf)
{
	return show_power_value(dev, REG_POWER, devattr, buf);
}


/* Sensor attributes supported by this device */

static SENSOR_DEVICE_ATTR(power1_max, 0644, show_power_max, set_power_max, 0);

static SENSOR_DEVICE_ATTR(power1_min, 0644, show_power_min, set_power_min, 0);

static SENSOR_DEVICE_ATTR(power1_input, 0444, show_power_input, NULL, 0);

static struct attribute *ltc2946_attrs[] = {
	&sensor_dev_attr_power1_max.dev_attr.attr,
        &sensor_dev_attr_power1_min.dev_attr.attr,
        &sensor_dev_attr_power1_input.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ltc2946);

static int ltc2946_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *i2c_dev = &client->dev;
	struct device *hwmon_dev;
	struct ltc2946_data *data;

	pr_err("Probing ltc2946");

	dev_info(&client->dev, "%s chip found\n", client->name);

	// Allocate this driver's memory
	data = devm_kzalloc(i2c_dev, sizeof(struct ltc2946_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	// INITIALIZE data HERE
	pr_err("Probing ltc2946");

	// Register the i2c client
	i2c_set_clientdata(client, data);
	data->client = client;

	// INITIALIZE DEVICE HERE

	// Register sysfs hooks
	hwmon_dev = devm_hwmon_device_register_with_groups(i2c_dev, client->name,
							   data, ltc2946_groups);

	if (IS_ERR(hwmon_dev))
	{
		return PTR_ERR(hwmon_dev);
	}

	return 0;
}

static int ltc2946_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id ltc2946_id[] = {
	{ "ltc2946", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c,ltc2946_id);

static const struct of_device_id ltc2946_dt_ids[] = {
	{ .compatible = "ltc,ltc2946" },
	{ }
};
MODULE_DEVICE_TABLE(of, ltc2946_dt_ids);

static struct i2c_driver ltc2946_driver = {
	.probe = ltc2946_probe,
	.remove = ltc2946_remove,
	.id_table = ltc2946_id,
	.driver = {
		.name = "ltc2946",
		.of_match_table = ltc2946_dt_ids,
	},
};
module_i2c_driver(ltc2946_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adam Schafer <adam@pseudo.design>");
MODULE_DESCRIPTION("LTC2946 Driver");

