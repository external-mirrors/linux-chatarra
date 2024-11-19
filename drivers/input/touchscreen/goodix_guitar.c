/*
 * Driver for Goodix GT8xx "Guitar" touchscreen controllers
 *
 * Copyright (c) 2015 Priit Laes <plaes@plaes.org>.
 * Copyright (c) 2024 Val Packett <val@packett.cool>.
 *
 * This code is based on goodix.c driver (c) 2014 Red Hat Inc,
 * various Android codedumps (c) 2010 - 2012 Goodix Technology
 * and cleanups done by Emilio López (turl) for linux-sunxi.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */
#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>

#define dev_dbg dev_err
#define dev_info dev_err

#define GOODIX_MAX_HEIGHT 4096
#define GOODIX_MAX_WIDTH 4096
#define GOODIX_INT_TRIGGER 1
#define GOODIX_MAX_CONTACTS 5 // the + has 10
// #define MAX_CONTACTS_LOC 5
// f80 start
#define MAX_CONTACTS_LOC 67 // fc3
// #define RESOLUTION_LOC 1
#define RESOLUTION_LOC 69 // fc5
// #define TRIGGER_LOC 6
#define TRIGGER_LOC 64 // fc0

#define X_BORDER_LIM_NEAR_LOC 96 //fe0
#define X_BORDER_LIM_FAR_LOC 97
#define Y_BORDER_LIM_NEAR_LOC 98
#define Y_BORDER_LIM_FAR_LOC 99

#define LOC(x) ((x) - 0xf80)

/* Register defines */
// #define guitar_COOR_ADDR 0x00 // the + starts at 0x01 0.o
// #define guitar_CONFIG_DATA 0x30 // the + starts at 0x65
// #define guitar_REG_ID 0xf0 // not a thing on 800/1/2 tho?

/* Device specific defines */
#define GUITAR_CONFIG_MAX_LENGTH 7
#define GUITAR_CONTACT_SIZE 5

static const unsigned long goodix_irq_flags[] = {
	IRQ_TYPE_EDGE_FALLING,
	IRQ_TYPE_EDGE_RISING,
};

struct guitar_protocol {
	int (*read_reg) (struct i2c_client *client, u16 reg, u8 *buf, int len);
	int (*write_reg)(struct i2c_client *client, u16 reg, u8 *buf, int len);
	int config_len;
	u16 reg_coor;
	u16 reg_config;
	u16 reg_id;
};

struct guitar_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct touchscreen_properties prop;
	struct guitar_protocol *protocol;
	int abs_x_max;
	int abs_y_max;
	unsigned int max_touch_num;
	unsigned int int_trigger_type;
};

static int guitar_i2c_read(struct i2c_client *client,
                           u16 reg, bool reg_u8, u8 *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret;
	__be16 wbuf = cpu_to_be16(reg);

	msgs[0].flags = 0;
	msgs[0].addr  = client->addr;
	msgs[0].len   = reg_u8 ? 1 : 2;
	msgs[0].buf   = &((u8 *)&wbuf)[reg_u8 ? 1 : 0];

	msgs[1].flags = I2C_M_RD;
	msgs[1].addr = client->addr;
	msgs[1].len = len;
	msgs[1].buf = buf;

	ret = i2c_transfer(client->adapter, msgs, 2);
	return ret < 0 ? ret : (ret != ARRAY_SIZE(msgs) ? -EIO : 0);
}

static int guitar_i2c_write(struct i2c_client *client,
                            u16 reg, bool reg_u8, u8 *buf, int len)
{
	u8 *addr_buf;
	struct i2c_msg msg;
	int ret;
	int reg_len = reg_u8 ? 1 : 2;

	addr_buf = kmalloc(len + reg_len, GFP_KERNEL);
	if (!addr_buf)
		return -ENOMEM;

	if (reg_u8)
		addr_buf[0] = reg & 0xFF;
	else
		put_unaligned_be16(reg, &addr_buf[0]);
	memcpy(&addr_buf[reg_len], buf, len);

	msg.flags = 0;
	msg.addr = client->addr;
	msg.buf = addr_buf;
	msg.len = len + reg_len;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		ret = (ret == 1 ? 0 : -EIO);

	kfree(addr_buf);

	if (ret)
		dev_err(&client->dev, "Error writing %d bytes to 0x%04x: %d\n",
			len, reg, ret);
	return ret;
}

static int guitar_i2c_read_v2(struct i2c_client *client,
                              u16 reg, u8 *buf, int len)
{
	int err = guitar_i2c_read(client, reg, false, buf, len);
	if (err)
		return err;
	err = guitar_i2c_write(client, 0x8000, false, NULL, 0);
	return err;
}

static int guitar_i2c_write_v2(struct i2c_client *client,
                               u16 reg, u8 *buf, int len)
{
	int err = guitar_i2c_write(client, reg, false, buf, len);
	if (err)
		return err;
	err = guitar_i2c_write(client, 0x8000, false, NULL, 0);
	return err;
}

struct guitar_protocol guitar_protocol_2 = {
	.read_reg = guitar_i2c_read_v2,
	.write_reg = guitar_i2c_write_v2,
	.config_len = 112,
	.reg_coor = 0x0F40,
	.reg_config = 0x0F80,
	.reg_id = 0x0F7D,
// #define GT82x_CONFIG_READY 0x0FEF
// #define GT82x_REG_MODULE_SUPPLIER 0x0FF5
};

static void guitar_write_config(struct guitar_ts_data *ts)
{
	ts->abs_x_max = 600;
	ts->abs_y_max = 1024;
	ts->max_touch_num = 5;
	ts->int_trigger_type = 0;

#define CFG_ONE {0x00,0x0F,0x01,0x10,0x02,0x11,0x03,0x12,0x04,0x13,0x05,0x14,0x06,0x15,0x07,0x16,0x08,0x17,0x09,0x18,0x0A,0x19,0x0B,0x1A,0x0C,0x1B,0x0D,0x1C,0x0E,0x1D,0x00,0x0A,0x01,0x0B,0x02,0x0C,0x03,0x0D,0x04,0x0E,0x05,0x0F,0x06,0x10,0x07,0x11,0x08,0x12,0x09,0x13,0x1F,0x03,0x88,0x10,0x10,0x29,0x00,0x00,0x09,0x00,0x00,0x0A,0x40,0x2D,0x2D,0x03,0x00,0x05,0x00,0x03,0x00,0x04,0x00,0x49,0x49,0x44,0x44,0x28,0x00,0x25,0x19,0x02,0x14,0x10,0x02,0xDA,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x60,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}
#define CFG_TWO {0x02,0x11,0x03,0x12,0x04,0x13,0x05,0x14,0x06,0x15,0x07,0x16,0x08,0x17,0x09,0x18,0x0A,0x19,0x0B,0x1A,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x12,0x08,0x11,0x07,0x10,0x06,0x0F,0x05,0x0E,0x04,0x0D,0x03,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x0F,0x03,0xE0,0x10,0x10,0x19,0x00,0x00,0x08,0x00,0x00,0x02,0x45,0x2D,0x1C,0x03,0x00,0x05,0x00,0x02,0x58,0x03,0x20,0x2D,0x38,0x2F,0x3B,0x25,0x00,0x06,0x19,0x25,0x14,0x10,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}
#define CFG_GHI {0x00,0x0f,0x01,0x10,0x02,0x11,0x03,0x12,0x04,0x13,0x05,0x14,0x06,0x15,0x07,0x16,0x08,0x17,0x09,0x18,0x0a,0x19,0x0b,0x1a,0x0c,0x1b,0x0d,0xff,0x1c,0x1d,0x02,0x0c,0x03,0x0d,0x04,0x0e,0x05,0x0f,0x06,0x10,0x07,0x11,0x08,0x12,0x09,0x13,0xff,0x0d,0x0e,0x0f,0x0f,0x03,0x10,0x10,0x10,0x20,0x20,0x20,0x02,0x02,0x02,0x02,0x43,0x22,0x35,0x03,0x00,0x05,0x00,0x02,0x58,0x04,0x00,0x3a,0x3c,0x3d,0x3e,0x05,0x00,0x2f,0x19,0x23,0x14,0x10,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}
	u8 konfig[] = CFG_GHI;
	u16 absx = get_unaligned_be16(&konfig[RESOLUTION_LOC]);
	u16 absy = get_unaligned_be16(&konfig[RESOLUTION_LOC + 2]);
	dev_err(&ts->client->dev, "reso was %lu x %lu\n", absx, absy);
	// put_unaligned_be16(ts->abs_x_max, &konfig[RESOLUTION_LOC]);
	// put_unaligned_be16(ts->abs_y_max, &konfig[RESOLUTION_LOC + 2]);
	dev_err(&ts->client->dev, "sito filter was %lu\n", konfig[LOC(0xfc0)] & BIT(2));
	dev_err(&ts->client->dev, "nosie r was %hhu\n", konfig[LOC(0xfcd)]);
	// konfig[LOC(0xfc0)]
	// konfig[X_BORDER_LIM_NEAR_LOC] = 255; // 1842
	// konfig[MAX_CONTACTS_LOC] = 5;
	// konfig[97] = 768-600;
	konfig[99] = 255;

	int error = ts->protocol->write_reg(ts->client, ts->protocol->reg_config, konfig, ts->protocol->config_len);
	if (error) {
		dev_err(&ts->client->dev, "Error writing config (%d)\n", error);
	}
}

/**
 * guitar_process_events - Process incoming events
 *
 * @ts: our guitar_ts_data pointer
 *
 * Called when the IRQ is triggered. Read the current device state, and push
 * the input events to the user space.
 */
static void guitar_process_events(struct guitar_ts_data *ts)
{
	u8 point_data[3 + GUITAR_CONTACT_SIZE * GOODIX_MAX_CONTACTS];
	u8 touch_map[GOODIX_MAX_CONTACTS] = {0};
	int input_x, input_y, input_w;
	u16 touch_raw;
	u8 touch_num;
	int err;
	int loc;
	int i;

	err = ts->protocol->read_reg(ts->client, ts->protocol->reg_coor,
	                       point_data, sizeof(point_data));
	if (err) {
		dev_err(&ts->client->dev, "I2C transfer error: %d\n", err);
		return;
	}

	/* Fetch touch mapping bits */
	dev_err(&ts->client->dev, "flags: %x key: %x annnd: %x %x %x %x %x %x %x %x\n",
	        point_data[0], point_data[1], point_data[2], point_data[3], point_data[4],
	        point_data[5], point_data[6], point_data[7], point_data[8], point_data[9]);

	u8 finger = point_data[0];

#if 0
	touch_raw = get_unaligned_le16(&point_data[0]);
	if (!touch_raw)
		return;

	/* Build touch map */
	touch_num = 0;
	for (i = 0; (touch_raw != 0) && (i < ts->max_touch_num); i++) {
		if (touch_raw & 1)
			touch_map[touch_num++] = i;
		touch_raw >>= 1;
	}
#endif

	/* Calculate checksum */
	u8 checksum = 0;
	touch_num = (finger & 0x01) + !!(finger & 0x02) + !!(finger & 0x04) + !!(finger & 0x08) + !!(finger & 0x10);
	for (i = 0; i < (touch_num * GUITAR_CONTACT_SIZE); i++)
		checksum += point_data[2 + i];
	if (checksum != point_data[2 + (touch_num * GUITAR_CONTACT_SIZE)]) {
		dev_err(&ts->client->dev, "csumer %x vs %x, fings %x\n", checksum, point_data[2 + (touch_num * GUITAR_CONTACT_SIZE)], finger & 0b11111);
		return;
	}

	/* Report touches */
	for (i = 0; i < GOODIX_MAX_CONTACTS; i++) {
		input_mt_slot(ts->input_dev, i);
		if ((finger & BIT(i)) == 0) {
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
			continue;
		}
		loc = 2 + GUITAR_CONTACT_SIZE * i;
		input_x = get_unaligned_be16(&point_data[loc]);
		input_y = get_unaligned_be16(&point_data[loc + 2]);
		input_w = point_data[loc + 4];

		dev_err(&ts->client->dev, "f%d: %d @ (%d, %d) \n", i, input_w, input_x, input_y);
		input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, i);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
		touchscreen_report_pos(ts->input_dev, &ts->prop,
		                       input_x, input_y, true);
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, input_w);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, input_w);
	}

	input_mt_sync_frame(ts->input_dev);
	input_sync(ts->input_dev);
}

/**
 * guitar_ts_irq_handler - The IRQ handler
 *
 * @irq: interrupt number.
 * @dev_id: private data pointer.
 */
static irqreturn_t guitar_ts_irq_handler(int irq, void *dev_id)
{
	struct guitar_ts_data *ts = dev_id;

	/*
	 * The 'buffer status' bit, which indicates that the data is valid, is
	 * not set as soon as the interrupt is raised, but slightly after.
	 * This takes around 10 ms to happen, so we poll for 20 ms.
	 */
#define GOODIX_BUFFER_STATUS_TIMEOUT	20
	unsigned long max_timeout;
	max_timeout = jiffies + msecs_to_jiffies(GOODIX_BUFFER_STATUS_TIMEOUT);
	int ls = 0;
	do {
		u8 status;
		int err = ts->protocol->read_reg(ts->client, ts->protocol->reg_coor,
		                      &status, sizeof(status));
		if (err) {
			dev_err(&ts->client->dev, "I2C transfer error: %d\n", err);
			return IRQ_HANDLED;
		}

		if (status & BIT(7)) {
			dev_err(&ts->client->dev, "rdy at loop %d st %x\n", ls, status);
			guitar_process_events(ts);
			return IRQ_HANDLED;
		}

		usleep_range(1000, 2000); /* Poll every 1 - 2 ms */
		ls++;
	} while (time_before(jiffies, max_timeout));


	return IRQ_HANDLED;
}

/**
 * guitar_read_config - Read the embedded configuration of the panel
 *
 * @ts: our guitar_ts_data pointer
 *
 * Must be called during probe
 */
static void guitar_read_config(struct guitar_ts_data *ts)
{
	u8 config[112];
	int error;

	error = ts->protocol->read_reg(ts->client, ts->protocol->reg_config, config, ts->protocol->config_len);
	if (error) {
		dev_warn(&ts->client->dev, "Error reading config (%d), using defaults\n", error);
		ts->abs_x_max = GOODIX_MAX_WIDTH;
		ts->abs_y_max = GOODIX_MAX_HEIGHT;
		ts->int_trigger_type = GOODIX_INT_TRIGGER;
		ts->max_touch_num = GOODIX_MAX_CONTACTS;
		return;
	}

	ts->abs_x_max = get_unaligned_be16(&config[RESOLUTION_LOC]);
	ts->abs_y_max = get_unaligned_be16(&config[RESOLUTION_LOC + 2]);
	// ts->int_trigger_type = config[TRIGGER_LOC] & 0x03;
	// ts->int_trigger_type = (config[TRIGGER_LOC] & 0x08) >> 3;
	ts->max_touch_num = config[MAX_CONTACTS_LOC] & 0x0f;
	dev_err(&ts->client->dev, "confi: %d*%d trig %x maxt %d\n", ts->abs_x_max, ts->abs_y_max, ts->int_trigger_type, ts->max_touch_num);
	if (!ts->abs_x_max || !ts->abs_y_max || !ts->max_touch_num) {
		dev_err(&ts->client->dev, "Invalid config, using defaults\n");
		// ts->abs_x_max = GOODIX_MAX_WIDTH;
		// ts->abs_y_max = GOODIX_MAX_HEIGHT;
		// ts->max_touch_num = GOODIX_MAX_CONTACTS;
	}
}

/**
 * guitar_read_version - Read touchscreen version
 *
 * @client: the i2c client
 * @version: output buffer containing the version on success
 * @id: output buffer containing the id on success
 */
static int guitar_read_version(struct guitar_ts_data *ts, u16 *version, u16 *id)
{
	int error;
	// u8 buf[16];
	u8 buf[3];

	error = ts->protocol->read_reg(ts->client, ts->protocol->reg_id, buf, sizeof(buf));
	if (error) {
		dev_err(&ts->client->dev, "read version failed: %d\n", error);
		return error;
	}
	/* TODO: version info contains 'GT801NI_3R15_1AV' */
	print_hex_dump_bytes("", DUMP_PREFIX_NONE, buf, ARRAY_SIZE(buf));
	// *id = 0x802;
	// *version = 0x15;
	*id = buf[0];
	*version = get_unaligned_be16(&buf[1]);
	dev_info(&ts->client->dev, "ID %d, version: %04x\n", *id, *version);
	return 0;
}

/**
 * guitar_i2c_test - I2C test function to check if the device answers.
 *
 * @client: the i2c client
 */
static int guitar_i2c_test(struct guitar_ts_data *ts)
{
	int retry = 0;
	int error;
	u8 test;

	while (retry++ < 5) {
		error = ts->protocol->read_reg(ts->client, ts->protocol->reg_config, &test, 1);
		// error = guitar_i2c_read(client, GT82x_CONFIG_DATA, &test, 1);
		if (!error) {
			dev_err(&ts->client->dev, "i2c test succ %d: %d\n", retry, error);
			return 0;
		}

		dev_err(&ts->client->dev, "i2c test failed attempt %d: %d\n", retry, error);
		msleep(20);
	}

	return error;
}

/**
 * guitar_request_input_dev - Allocate, populate and register the input device
 *
 * @ts: our guitar_ts_data pointer
 * @version: device firmware version
 * @id: device ID
 *
 * Must be called during probe
 */
static int guitar_request_input_dev(struct guitar_ts_data *ts,
 u16 version, u16 id)
{
	int error;

	ts->input_dev = devm_input_allocate_device(&ts->client->dev);
	if (!ts->input_dev) {
		dev_err(&ts->client->dev, "Failed to allocate input device.");
		return -ENOMEM;
	}

	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X,
	                     0, ts->abs_x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
	                     0, ts->abs_y_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);

	input_mt_init_slots(ts->input_dev, ts->max_touch_num,
	                    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);

	ts->input_dev->name = "Goodix Guitar Capacitive TouchScreen";
	ts->input_dev->phys = "input/ts";
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0x0416;
	ts->input_dev->id.product = id;
	ts->input_dev->id.version = version;

	error = input_register_device(ts->input_dev);
	if (error) {
		dev_err(&ts->client->dev,
		        "Failed to register input device: %d", error);
		return error;
	}

	return 0;
}

static int guitar_ts_probe(struct i2c_client *client)
{
	struct guitar_ts_data *ts;
	unsigned long irq_flags;
	int error;
	u16 version_info, id_info;

	dev_dbg(&client->dev, "I2C Address: 0x%02x\n", client->addr);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C check functionality failed.\n");
		return -ENXIO;
	}

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->protocol = (struct guitar_protocol*)device_get_match_data(&client->dev);
	if (!ts->protocol)
		return -ENXIO;

	ts->client = client;
	i2c_set_clientdata(client, ts);

	struct gpio_desc *rst = devm_gpiod_get(&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(rst))
		return dev_err_probe(&client->dev, PTR_ERR(rst), "Failed to get reset pin\n");

	gpiod_direction_output(rst, 0);
	gpiod_set_value_cansleep(rst, 0);
	msleep(150);
	gpiod_set_value_cansleep(rst, 1);
	msleep(150);
	// gpiod_direction_input(rst);
	// msleep(50);

	error = guitar_i2c_test(ts);
	if (error) {
		dev_err(&client->dev, "I2C communication failure: %d\n", error);
		return error;
	}

	error = guitar_read_version(ts, &version_info, &id_info);
	if (error) {
		dev_err(&client->dev, "Read version failed.\n");
		return error;
	}

	guitar_read_config(ts);
	guitar_write_config(ts);

	error = guitar_request_input_dev(ts, version_info, id_info);
	if (error)
		return error;

	touchscreen_parse_properties(ts->input_dev, true, &ts->prop);
	dev_err(&client->dev, "x %d - %d\n", ts->input_dev->absinfo[ABS_MT_POSITION_X].minimum, ts->input_dev->absinfo[ABS_MT_POSITION_X].maximum);
	dev_err(&client->dev, "y %d - %d\n", ts->input_dev->absinfo[ABS_MT_POSITION_Y].minimum, ts->input_dev->absinfo[ABS_MT_POSITION_Y].maximum);

// [  422.832431] goodix-guitar 2-005d: x 0 - 1921
// [  422.832473] goodix-guitar 2-005d: y 0 - 1115

	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X,
	                     0, 600, 0, 0); // XXX
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
	                     0, 1024, 0, 0); // XXX

	ts->prop.max_x = 600;
	ts->prop.max_y = 1024;
	unsigned int axis_x = ABS_MT_POSITION_X;
	unsigned int axis_y = ABS_MT_POSITION_Y;
	struct input_absinfo *absinfo;
	absinfo = &ts->input_dev->absinfo[axis_x];
	absinfo->maximum -= absinfo->minimum;
	absinfo->minimum = 0;
	absinfo = &ts->input_dev->absinfo[axis_y];
	absinfo->maximum -= absinfo->minimum;
	absinfo->minimum = 0;
	swap(ts->input_dev->absinfo[axis_x], ts->input_dev->absinfo[axis_y]);

	// irq_flags = goodix_irq_flags[ts->int_trigger_type] | IRQF_ONESHOT;
	irq_flags = goodix_irq_flags[0] | IRQF_ONESHOT;
	error = devm_request_threaded_irq(&client->dev, client->irq,
	                                  NULL, guitar_ts_irq_handler,
	                                  irq_flags, client->name, ts);
	if (error) {
		dev_err(&client->dev, "request IRQ failed: %d\n", error);
		return error;
	}

	return 0;
}

static void guitar_ts_remove(struct i2c_client *client)
{
	struct guitar_ts_data *ts = i2c_get_clientdata(client);

	devm_free_irq(&client->dev, client->irq, ts);
	input_unregister_device(ts->input_dev);
	input_free_device(ts->input_dev);
	i2c_set_clientdata(client, NULL);
	// kfree(ts);
}

static const struct of_device_id guitar_of_match[] = {
	{ .compatible = "goodix,gt813", .data = &guitar_protocol_2 },
	{ .compatible = "goodix,gt827", .data = &guitar_protocol_2 },
	{ .compatible = "goodix,gt828", .data = &guitar_protocol_2 },
	{ }
};
MODULE_DEVICE_TABLE(of, guitar_of_match);

static struct i2c_driver guitar_ts_driver = {
	.probe = guitar_ts_probe,
	.remove = guitar_ts_remove,
	.driver = {
		.name = "goodix-guitar",
		.of_match_table = of_match_ptr(guitar_of_match),
	},
};
module_i2c_driver(guitar_ts_driver);

MODULE_AUTHOR("Val Packett <val@packett.cool>");
MODULE_DESCRIPTION("Goodix GT8xx touchscreen driver");
MODULE_LICENSE("GPL v2"); 
