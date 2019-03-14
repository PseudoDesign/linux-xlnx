#include <linux/module.h>   /* Needed by all modules */
#include <linux/init.h>     /* Needed for the macros */
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/util_macros.h>
#include <linux/i2c.h>


struct ltc2946_data {
	struct i2c_client *client;
};

static int ltc2946_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *i2c_dev = &client->dev;
	struct device *hwmon_dev;
	struct ltc2946_data *data;

	dev_info(&client->dev, "%s chip found\n", client->name);

	// Allocate this driver's memory
	data = devm_kzalloc(i2c_dev, sizeof(struct ltc2946_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	// INITIALIZE data HERE

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


static struct i2c_driver ltc2946_driver = {
	.probe = ltc2946_probe,
	.remove = ltc2946_remove,
	//.id_table = ltc2946_id,
	.driver = {
		.name = "ltc2946",
		//.of_match_table = ltc2946_dt_ids,
	},
};
module_i2c_driver(ltc2946_driver);



MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adam Schafer <adam@pseudo.design>");
MODULE_DESCRIPTION("LTC2946 Driver");

