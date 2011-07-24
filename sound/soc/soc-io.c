/*
 * soc-io.c  --  ASoC register I/O helpers
 *
 * Copyright 2009-2011 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include <trace/events/asoc.h>

static int hw_write(struct snd_soc_codec *codec, unsigned int reg,
		    unsigned int value)
{
	int ret;

	if (!snd_soc_codec_volatile_register(codec, reg) &&
	    reg < codec->driver->reg_cache_size &&
	    !codec->cache_bypass) {
		ret = snd_soc_cache_write(codec, reg, value);
		if (ret < 0)
			return -1;
	}

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	return regmap_write(codec->control_data, reg, value);
}

static unsigned int hw_read(struct snd_soc_codec *codec, unsigned int reg)
{
	int ret;
	unsigned int val;

	if (reg >= codec->driver->reg_cache_size ||
	    snd_soc_codec_volatile_register(codec, reg) ||
	    codec->cache_bypass) {
		if (codec->cache_only)
			return -1;

		ret = regmap_read(codec->control_data, reg, &val);
		if (ret == 0)
			return val;
		else
			return ret;
	}

	ret = snd_soc_cache_read(codec, reg, &val);
	if (ret < 0)
		return -1;
	return val;
}

/* Primitive bulk write support for soc-cache.  The data pointed to by
 * `data' needs to already be in the form the hardware expects.  Any
 * data written through this function will not go through the cache as
 * it only handles writing to volatile or out of bounds registers.
 *
 * This is currently only supported for devices using the regmap API
 * wrappers.
 */
static int snd_soc_hw_bulk_write_raw(struct snd_soc_codec *codec,
				     unsigned int reg,
				     const void *data, size_t len)
{
	/* To ensure that we don't get out of sync with the cache, check
	 * whether the base register is volatile or if we've directly asked
	 * to bypass the cache.  Out of bounds registers are considered
	 * volatile.
	 */
	if (!codec->cache_bypass
	    && !snd_soc_codec_volatile_register(codec, reg)
	    && reg < codec->driver->reg_cache_size)
		return -EINVAL;

	return regmap_raw_write(codec->control_data, reg, data, len);
}

/**
 * snd_soc_codec_set_cache_io: Set up standard I/O functions.
 *
 * @codec: CODEC to configure.
 * @addr_bits: Number of bits of register address data.
 * @data_bits: Number of bits of data per register.
 * @control: Control bus used.
 *
 * Register formats are frequently shared between many I2C and SPI
 * devices.  In order to promote code reuse the ASoC core provides
 * some standard implementations of CODEC read and write operations
 * which can be set up using this function.
 *
 * The caller is responsible for allocating and initialising the
 * actual cache.
 *
 * Note that at present this code cannot be used by CODECs with
 * volatile registers.
 */
int snd_soc_codec_set_cache_io(struct snd_soc_codec *codec,
			       int addr_bits, int data_bits,
			       enum snd_soc_control_type control)
{
	struct regmap_config config;

	memset(&config, 0, sizeof(config));
	codec->write = hw_write;
	codec->read = hw_read;
	codec->bulk_write_raw = snd_soc_hw_bulk_write_raw;

	config.reg_bits = addr_bits;
	config.val_bits = data_bits;

	switch (control) {
	case SND_SOC_I2C:
		codec->control_data = regmap_init_i2c(to_i2c_client(codec->dev),
						      &config);
		break;

	case SND_SOC_SPI:
		codec->control_data = regmap_init_spi(to_spi_device(codec->dev),
						      &config);
		break;

	case SND_SOC_REGMAP:
		/* Device has made its own regmap arrangements */
		break;

	default:
		return -EINVAL;
	}

	if (IS_ERR(codec->control_data))
		return PTR_ERR(codec->control_data);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_set_cache_io);

