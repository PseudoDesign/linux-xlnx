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

#define REG_VOLTAGE_MAX			0x2C
#define REG_VOLTAGE_MIN			0x2A
#define REG_VOLTAGE			0x28
#define VOLTAGE_VALUE_TO_MICROVOLT	500

#define REG_SENSE_MAX			0x16
#define REG_SENSE_MIN			0x18
#define REG_SENSE			0x14
#define SENSE_RESISTANCE_MICROOHM_DEFAULT 1000
#define SENSE_VALUE_TO_NANOVOLT		25000

struct ltc2946_data {
        struct i2c_client *client;
	uint32_t sense_resistance;
	uint32_t adin_r1;
	uint32_t adin_r2;
};

/* Functions supporting the i2c transactions */
static unsigned int read_uint24(struct i2c_client *client, u8 address)
{
	u8 bytes[3] = {0, 0, 0};

	i2c_smbus_read_i2c_block_data(client, address, 3, bytes);
	//pr_err("Read %x, %x, %x from register %x", bytes[0], bytes[1], bytes[2], address);
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

	//pr_err("Writing %x, %x, %x to register %x", bytes[0], bytes[1], bytes[2], address);
	return i2c_smbus_write_i2c_block_data(client, address, 3, bytes);
}

static unsigned int read_uint12(struct i2c_client *client, u8 address)
{
	u8 bytes[2] = {0, 0};

	i2c_smbus_read_i2c_block_data(client, address, 2, bytes);
	//pr_err("Read %x, %x from register %x", bytes[0], bytes[1], address);

	return 0xFFF & ((0xFF0 & (bytes[0] << 4)) + (0xF & (bytes[1] >> 4)));
}

static int write_uint12(struct i2c_client *client, u8 address, unsigned int value)
{
	unsigned int input = clamp_val(value, 0, 0xFFF);

	u8 bytes[] = {
		0xFF & (input >> 4),
		0xF0 & (input << 4)
	};

	//pr_err("Writing %x, %x to register %x", bytes[0], bytes[1], address);

	return i2c_smbus_write_i2c_block_data(client, address, 2, bytes);
}

/* Functions supporting the sensor attributes */

/* Power Attributes */

static ssize_t show_power_value(struct device *dev, u8 address, struct device_attribute *devattr, char *buf)
{
	struct ltc2946_data *data = dev_get_drvdata(dev);
        unsigned long output = read_uint24(data->client, address);
	//pr_err("Read (%ld) from power reg", output);
	output *= POWER_VALUE_TO_NWATT;
        return sprintf(buf, "%ld\n", output / 1000);
}

static ssize_t set_power_value(struct device *dev, u8 address, struct device_attribute *devattr, const char *buf, size_t count)
{
	long input;
        int retval;
        struct ltc2946_data *data = dev_get_drvdata(dev);

	if (kstrtol(buf, 10, &input))
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

/* Voltage Attributes */

static ssize_t show_voltage_value(struct device *dev, u8 address, struct device_attribute *devattr, char *buf)
{
	struct ltc2946_data *data = dev_get_drvdata(dev);
        unsigned long output = read_uint12(data->client, address);
	//pr_err("Read (%ld) from voltage reg", output);
	output *= VOLTAGE_VALUE_TO_MICROVOLT;
	output /= 1000;
	//pr_err("Read %ld mV at Vsense", output);
	// Apply the voltage divider
	//pr_err("r1=%d, r2=%d", data->adin_r1, data->adin_r2);
	output = (output * (data->adin_r1 + data->adin_r2)) / data->adin_r2;
        return sprintf(buf, "%ld\n", output);
}

static ssize_t set_voltage_value(struct device *dev, u8 address, struct device_attribute *devattr, const char *buf, size_t count)
{
	long input;
        int retval;
        struct ltc2946_data *data = dev_get_drvdata(dev);

	if (kstrtol(buf, 10, &input))
		return -EINVAL;

	input /= VOLTAGE_VALUE_TO_MICROVOLT;
	input /= 1000;
	input = (input * data->adin_r2) / (data->adin_r1 + data->adin_r2);
	retval = write_uint12(data->client, address, (unsigned int)input);
        if (retval < 0)
                return retval;

        return count;
}

static ssize_t show_voltage_max(struct device *dev, struct device_attribute *devattr, char *buf)
{
	return show_voltage_value(dev, REG_VOLTAGE_MAX, devattr, buf);
}

static ssize_t set_voltage_max(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	return set_voltage_value(dev, REG_VOLTAGE_MAX, devattr, buf, count);
}

static ssize_t show_voltage_min(struct device *dev, struct device_attribute *devattr, char *buf)
{
	return show_voltage_value(dev, REG_VOLTAGE_MIN, devattr, buf);
}

static ssize_t set_voltage_min(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
	return set_voltage_value(dev, REG_VOLTAGE_MIN, devattr, buf, count);
}

static ssize_t show_voltage_input(struct device *dev, struct device_attribute *devattr, char *buf)
{
	return show_voltage_value(dev, REG_VOLTAGE, devattr, buf);
}

/* Current registers */

static ssize_t show_current_value(struct device *dev, u8 address, struct device_attribute *devattr, char *buf)
{
        struct ltc2946_data *data = dev_get_drvdata(dev);
        unsigned long output = read_uint12(data->client, address) * SENSE_VALUE_TO_NANOVOLT;
	// We have nanovolts in "output".  Divide by the sense resistance in microohm to get the current in milliamps
	output /= data->sense_resistance;	
        return sprintf(buf, "%ld\n", output);
}

static ssize_t set_current_value(struct device *dev, u8 address, struct device_attribute *devattr, const char *buf, size_t count)
{
        long input;
        int retval;
        struct ltc2946_data *data = dev_get_drvdata(dev);

        if (kstrtol(buf, 10, &input))
                return -EINVAL;

        //Multiply our current input (milliamps) by our sense resiancte (microohm) to get the voltage in nanovolts
	input = input * data->sense_resistance;
	// Divide by nanovolts per count to get the correct register value
	input = input / SENSE_VALUE_TO_NANOVOLT;
	retval = write_uint12(data->client, address, (unsigned int)input);
        if (retval < 0)
                return retval;

        return count;
}


static ssize_t show_current_max(struct device *dev, struct device_attribute *devattr, char *buf)
{
        return show_current_value(dev, REG_SENSE_MAX, devattr, buf);
}

static ssize_t set_current_max(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
        return set_current_value(dev, REG_SENSE_MAX, devattr, buf, count);
}

static ssize_t show_current_min(struct device *dev, struct device_attribute *devattr, char *buf)
{
        return show_current_value(dev, REG_SENSE_MIN, devattr, buf);
}

static ssize_t set_current_min(struct device *dev, struct device_attribute *devattr, const char *buf, size_t count)
{
        return set_current_value(dev, REG_SENSE_MIN, devattr, buf, count);
}

static ssize_t show_current_input(struct device *dev, struct device_attribute *devattr, char *buf)
{
        return show_current_value(dev, REG_SENSE, devattr, buf);
}


/* Sensor attributes supported by this device */

static SENSOR_DEVICE_ATTR(power1_max, 0644, show_power_max, set_power_max, 0);
static SENSOR_DEVICE_ATTR(power1_min, 0644, show_power_min, set_power_min, 0);
static SENSOR_DEVICE_ATTR(power1_input, 0444, show_power_input, NULL, 0);

static SENSOR_DEVICE_ATTR(in1_max, 0644, show_voltage_max, set_voltage_max, 0);
static SENSOR_DEVICE_ATTR(in1_min, 0644, show_voltage_min, set_voltage_min, 0);
static SENSOR_DEVICE_ATTR(in1_input, 0444, show_voltage_input, NULL, 0);

static SENSOR_DEVICE_ATTR(curr1_max, 0644, show_current_max, set_current_max, 0);
static SENSOR_DEVICE_ATTR(curr1_min, 0644, show_current_min, set_current_min, 0);
static SENSOR_DEVICE_ATTR(curr1_input, 0444, show_current_input, NULL, 0);

static struct attribute *ltc2946_attrs[] = {
	&sensor_dev_attr_power1_max.dev_attr.attr,
	&sensor_dev_attr_power1_min.dev_attr.attr,
	&sensor_dev_attr_power1_input.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_max.dev_attr.attr,
        &sensor_dev_attr_curr1_min.dev_attr.attr,
        &sensor_dev_attr_curr1_input.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ltc2946);

static int ltc2946_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *i2c_dev = &client->dev;
	struct device *hwmon_dev;
	struct ltc2946_data *data;
	int retval;
	u8 byte;

	dev_info(&client->dev, "%s chip found\n", client->name);

	// Allocate this driver's memory
	data = devm_kzalloc(i2c_dev, sizeof(struct ltc2946_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	// Register the i2c client
	i2c_set_clientdata(client, data);
	data->client = client;

	// Get our sense resistance property, or use the default
	retval = of_property_read_u32(client->dev.of_node, "sense-resistance-microohm",
				  &data->sense_resistance);
	if (retval)
		data->sense_resistance = SENSE_RESISTANCE_MICROOHM_DEFAULT;

	retval = of_property_read_u32(client->dev.of_node, "adin-div-r1",
                                  &data->adin_r1);

	if (retval)
                data->adin_r1 = 1;

	retval = of_property_read_u32(client->dev.of_node, "adin-div-r2",
                                  &data->adin_r2);

        if (retval)
                data->adin_r2 = 1000;

	// Register sysfs hooks
	hwmon_dev = devm_hwmon_device_register_with_groups(i2c_dev, client->name,
							   data, ltc2946_groups);

	//Set CTRLA register to use ADIN
	byte = 0x10;
	i2c_smbus_write_i2c_block_data(client, 0, 1, &byte);

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
