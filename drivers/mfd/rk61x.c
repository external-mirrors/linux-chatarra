// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2024 Val Packett <val@packett.cool>
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>



#if 0

#define RK610_CONTROL_REG_C_PLL_CON0	0x00
#define RK610_CONTROL_REG_C_PLL_CON1	0x01
#define RK610_CONTROL_REG_C_PLL_CON2	0x02
#define RK610_CONTROL_REG_C_PLL_CON3	0x03
#define RK610_CONTROL_REG_C_PLL_CON4	0x04
#define RK610_CONTROL_REG_C_PLL_CON5	0x05
	#define C_PLL_DISABLE_FRAC		1 << 0
	#define C_PLL_BYPSS_ENABLE		1 << 1
	#define C_PLL_POWER_ON			1 << 2
	#define C_PLL_LOCLED			1 << 7
	
#define RK610_CONTROL_REG_TVE_CON		0x29
	#define TVE_CONTROL_VDAC_R_BYPASS_ENABLE	1 << 7
	#define TVE_CONTROL_VDAC_R_BYPASS_DISABLE	0 << 7
	#define TVE_CONTROL_CVBS_3_CHANNEL_ENALBE	1 << 6
	#define TVE_CONTROL_CVBS_3_CHANNEL_DISALBE	0 << 5
enum {
	INPUT_DATA_FORMAT_RGB888 = 0,
	INPUT_DATA_FORMAT_RGB666,
	INPUT_DATA_FORMAT_RGB565,
	INPUT_DATA_FORMAT_YUV
};
	#define RGB2CCIR_INPUT_DATA_FORMAT(n)	n << 4
	
	#define RGB2CCIR_RGB_SWAP_ENABLE		1 << 3
	#define RGB2CCIR_RGB_SWAP_DISABLE		0 << 3
	
	#define RGB2CCIR_INPUT_INTERLACE		1 << 2
	#define RGB2CCIR_INPUT_PROGRESSIVE		0 << 2
	
	#define RGB2CCIR_CVBS_PAL				0 << 1
	#define RGB2CCIR_CVBS_NTSC				1 << 1
	
	#define RGB2CCIR_DISABLE				0
	#define RGB2CCIR_ENABLE					1
	
#define RK610_CONTROL_REG_CCIR_RESET	0x2a

#define RK610_CONTROL_REG_CLOCK_CON0	0x2b
#define RK610_CONTROL_REG_CLOCK_CON1	0x2c
	#define CLOCK_CON1_I2S_CLK_CODEC_PLL	1 << 5
	#define CLOCK_CON1_I2S_DVIDER_MASK		0x1F
#define RK610_CONTROL_REG_CODEC_CON		0x2d
	#define CODEC_CON_BIT_HDMI_BLCK_INTERANL		1<<4
	#define CODEC_CON_BIT_DAC_LRCL_OUTPUT_DISABLE	1<<3
	#define CODEC_CON_BIT_ADC_LRCK_OUTPUT_DISABLE	1<<2
	#define CODEC_CON_BIT_INTERAL_CODEC_DISABLE		1<<0

#define RK610_CONTROL_REG_I2C_CON		0x2e

/* CODEC PLL REG */
#define C_PLL_CON0      0x00
#define C_PLL_CON1      0x01
#define C_PLL_CON2      0x02
#define C_PLL_CON3      0x03
#define C_PLL_CON4      0x04
#define C_PLL_CON5      0x05

/*  SCALER PLL REG */

/*  LVDS REG */

/*  LCD1 REG */

/*  SCALER REG  */
#define SCL_CON1        0x0d
#define SCL_CON2        0x0e
#define SCL_CON3        0x0f
#define SCL_CON4        0x10
#define SCL_CON5        0x11
#define SCL_CON6        0x12
#define SCL_CON7        0x13
#define SCL_CON8        0x14
#define SCL_CON9        0x15
#define SCL_CON10       0x16
#define SCL_CON11       0x17
#define SCL_CON12       0x18
#define SCL_CON13       0x19
#define SCL_CON14       0x1a
#define SCL_CON15       0x1b
#define SCL_CON16       0x1c
#define SCL_CON17       0x1d
#define SCL_CON18       0x1e
#define SCL_CON19       0x1f
#define SCL_CON20       0x20
#define SCL_CON21       0x21
#define SCL_CON22       0x22
#define SCL_CON23       0x23
#define SCL_CON24       0x24
#define SCL_CON25       0x25
#define SCL_CON26       0x26
#define SCL_CON27       0x27
#define SCL_CON28       0x28

/*  TVE REG  */
#define TVE_CON         0x29

/*  CCIR REG    */
#define CCIR_RESET      0X2a

/*  CLOCK REG    */
#define CLOCK_CON1      0X2c

/*  CODEC REG    */
#define CODEC_CON       0x2e
#define I2C_CON         0x2f

//SCALER config
#define NOBYPASS    0
#define BYPASS      1

//SCALER PLL config
#define S_PLL_PWR_ON    0
#define S_PLL_PWR_DOWN  1

#define S_PLL_UNLOCK            (0<<7)    //0:unlock 1:pll_lock
#define S_PLL_LOCK              (1<<7)    //0:unlock 1:pll_lock
#define S_PLL_PWR(x)            (((x)&1)<<2)    //0:POWER UP 1:POWER DOWN
#define S_PLL_RESET(x)          (((x)&1)<<1)    //0:normal  1:reset M/N dividers
#define S_PLL_BYPASS(x)          (((x)&1)<<0)    //0:normal  1:bypass// pll_en

c = S_PLL_PWR(1)|S_PLL_RESET(0)|S_PLL_BYPASS(1); // 0 to use, 5 to bypass
rk610_scaler_write_p0_reg(client, S_PLL_CON2, &c);


// scl_con0 says lcd h lol
#define SCL_BYPASS(x)           (((x)&1)<<4)    //0:not bypass  1:bypass
#define SCL_DEN_INV(x)          (((x)&1)<<3)    //scl_den_inv
#define SCL_H_V_SYNC_INV(x)     (((x)&1)<<2)    //scl_sync_inv
#define SCL_OUT_CLK_INV(x)      (((x)&1)<<1)    //scl_dclk_inv
#define SCL_ENABLE(x)           (((x)&1)<<0)    //scaler enable
	screen->s_clk_inv = S_DCLK_POL; // 1
	screen->s_den_inv = 0;
	screen->s_hv_sync_inv = 0;
    c= SCL_BYPASS(0) |SCL_DEN_INV(den_inv) |SCL_H_V_SYNC_INV(hv_sync_inv) |SCL_OUT_CLK_INV(clk_inv) |SCL_ENABLE(ENABLE);  
    // no bypass 1
    // bypass 0x10
	rk610_scaler_write_p0_reg(client, SCL_CON0, &c);


#define LCD1_AS_IN      0
#define LCD1_AS_OUT     1
#define LCD1_OUT_ENABLE(x)      (((x)&1)<<1)    //0:lcd1 as input 1:lcd1 as output
#define LCD1_OUT_SRC(x)         (((x)&1)<<0)    //0:from lcd0   1:from scaler
		c = LCD1_OUT_ENABLE(LCD1_AS_IN);
		// 0. was already 0 // not touched for lvds
#define LCD1_CON        0x0b
		rk610_scaler_write_p0_reg(client, LCD1_CON, &c);

      rk610_scaler_pll_set(client,screen,148500000);
      scale_hv_factor(client,1920,screen->x_res,1080,screen->y_res);
#define S_PLL_FROM_DIV      0
#define S_PLL_FROM_CLKIN    1
#define S_PLL_DIV(x)        ((x)&0x7)
    c = S_PLL_FROM_DIV<<3 | S_PLL_DIV(0);
#define CLOCK_CON0      0X2b
	rk610_scaler_write_p0_reg(client, CLOCK_CON0, &c);

	// dmesg: 57000000 or 74250000
    OD = (screen->s_pixclock)&0x3; // 0
    N = (screen->s_pixclock >>4)&0xf; // 4
#define S_DIV_N(x)              (((x)&0xf)<<4)
#define S_DIV_OD(x)             (((x)&3)<<0)
    c = S_DIV_N(N)| S_DIV_OD(OD); // 0x40
#define S_PLL_CON0      0x06
	rk610_scaler_write_p0_reg(client, S_PLL_CON0, &c);

    M = (screen->s_pixclock >>8)&0xff; //192
#define S_DIV_M(x)              ((x)&0xff)
    c = S_DIV_M(M); // 0xc0
#define S_PLL_CON1      0x07
  rk610_scaler_write_p0_reg(client, S_PLL_CON1, &c);

// sudo i2cset -y 1 0x40 0x09 0x54 && sudo i2cset -y 1 0x40 0x0a 0x00 && sudo i2cset -y 1 0x40 0x0c 0x10 &&
// sudo i2cset -y 1 0x40 0x08 0x05
#endif

#define LVDS_CON1       0x0a
#define LVDS_ENABLED       0
#define LVDS_DISABLED      ((1 << 4) | 1)

//LVDS lane input format
#define DATA_D0_MSB         0
#define DATA_D7_MSB         1
//LVDS input source
#define FROM_LCD1           0
#define FROM_LCD0_OR_SCL    1
//LCD1 output source
#define LCD1_FROM_LCD0  0
#define LCD1_FROM_SCL   1//LVDS_CON0
#define LVDS_OUT_CLK_PIN(x)     (((x)&1)<<7)    //clk enable pin, 0: enable
#define LVDS_OUT_CLK_PWR_PIN(x) (((x)&1)<<6)    //clk pwr enable pin, 1: enable 
#define LVDS_PLL_PWR_PIN(x)     (((x)&1)<<5)    //pll pwr enable pin, 0:enable 
#define LVDS_BIASE_PWR(x)       (((x)&1)<<4)    //0: power down     1: normal work
#define LVDS_LANE_IN_FORMAT(x)  (((x)&1)<<3)    //0: msb on D0  1:msb on D7
#define LVDS_INPUT_SOURCE(x)    (((x)&1)<<2)    //0: from lcd1  1:from lcd0 or scaler
#define LVDS_OUTPUT_FORMAT(x)   (((x)&3)<<0)    //00:8bit format-1  01:8bit format-2  10:8bit format-3   11:6bit format  
#define LVDS_CON0       0x09

#define SCL_CON0        0x0c
#define S_PLL_CON2      0x08

static const struct regmap_config rk61x_control_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	// .max_register = 0x25,
};

struct rk61x_priv {
	struct regmap *regmap;
};

static void rk61x_display_scaler_bypass(struct rk61x_priv *priv, bool bypass) {
	regmap_write(priv->regmap, SCL_CON0, bypass ? 0x10 : 0x01);
	regmap_write(priv->regmap, S_PLL_CON2, bypass ? 0x08 : 0x00 /*idk*/);
};

// TODO allow FROM_LCD1
// TODO LVDS_OUTPUT_FORMAT(screen->lvds_format), LVDS_LANE_IN_FORMAT(*)
static void rk61x_display_configure_lvds(struct rk61x_priv *priv, bool enable) {
	regmap_write(priv->regmap, LVDS_CON0,
		  LVDS_OUT_CLK_PIN(enable ? 0 : 1)
		| LVDS_OUT_CLK_PWR_PIN(enable ? 1 : 0)
		| LVDS_PLL_PWR_PIN(enable ? 0 : 1)
		| LVDS_LANE_IN_FORMAT(DATA_D0_MSB)
		| LVDS_BIASE_PWR(enable ? 1 : 0)
		| LVDS_INPUT_SOURCE(FROM_LCD0_OR_SCL)
	);
	regmap_write(priv->regmap, LVDS_CON1, LVDS_ENABLED);
}

static int rk61x_probe(struct i2c_client *i2c)
{
	struct rk61x_priv *priv = devm_kzalloc(&i2c->dev,
		sizeof(struct rk61x_priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	struct gpio_desc *rst = devm_gpiod_get(&i2c->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(rst))
		return dev_err_probe(&i2c->dev, PTR_ERR(rst), "Failed to get reset pin\n");

	gpiod_direction_output(rst, 1);
	gpiod_set_value_cansleep(rst, 1);
	msleep(100);
	gpiod_set_value_cansleep(rst, 0);
	msleep(100);
	gpiod_set_value_cansleep(rst, 1);

	priv->regmap = devm_regmap_init_i2c(i2c, &rk61x_control_regmap_config);
	if (IS_ERR(priv->regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(priv->regmap),
		                     "Failed to allocate regmap\n");

	i2c_set_clientdata(i2c, priv);

	rk61x_display_scaler_bypass(priv, true);
	rk61x_display_configure_lvds(priv, true);

	// const struct rk61x_platform_data *data;
	// data = device_get_match_data(&client->dev);
	// if (!data)
	// 	return -ENODEV;

	return 0;
}


// static SIMPLE_DEV_PM_OPS(rk61x_pm_ops, rk61x_suspend, rk61x_resume);

static const struct of_device_id rk61x_of_match[] = {
	{ .compatible = "rockchip,rk610" },
	// .data = &silergy_sy7636a,
	{}
};
MODULE_DEVICE_TABLE(of, rk61x_of_match);

static struct i2c_driver rk61x_driver = {
	.probe = rk61x_probe,
	.driver = {
		.name = "rk61x",
		.of_match_table = rk61x_of_match,
		// .pm = &rk61x_pm_ops,
	},
};
module_i2c_driver(rk61x_driver);

MODULE_AUTHOR("Val Packett <val@packett.cool>");
MODULE_DESCRIPTION("RK610 I2C MFD driver");
MODULE_LICENSE("GPL v2");
