// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim max11108 ADC Driver with IIO interface
 *
 * Copyright (C) 2021 CompuLab Ltd.
 */
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

/*
 * LSB is the ADC single digital step
 * 1 LSB = (vref_mv / 2 ^ 12)
 *
 * LSB is used to calculate analog voltage value
 * from the number of ADC steps count
 *
 * Ain = (count * LSB)
 */
#define MAX11108_LSB_DIV		(1 << 12)

struct max11108_state {
	struct regulator *vref_reg;
	struct spi_device *spi;
	struct iio_trigger *trig;
	struct mutex lock;
};

static struct iio_chan_spec max11108_channels[] = {
	{ /* [0] */
		.type = IIO_CURRENT,
		.channel = 0,
		.info_mask_separate =	BIT(IIO_CHAN_INFO_RAW) |
					BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 12,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
};

static int max11108_read_single(struct iio_dev *indio_dev, int *val)
{
	int ret;
	u32 uval;
	u8 buffer[3];
	struct max11108_state *state = iio_priv(indio_dev);

	ret = spi_read(state->spi, buffer, sizeof(buffer));
	if (ret) {
		dev_err(&indio_dev->dev, "SPI transfer failed\n");
		return ret;
	}

	/* The last 8 bits sent out from ADC must be 0s */
	if (buffer[2]) {
		dev_err(&indio_dev->dev, "Invalid value: buffer[0] != 0\n");
		return -EINVAL;
	}

	/* Raw date composition */
	uval = (buffer[0] << 8) | buffer[1];
	uval >>= 1;
	uval &= 0xfff;
	/* Store result */
	*((u32*) val) = uval;

	return 0;
}

static int max11108_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long info)
{
	int ret, vref_uv;
	struct max11108_state *state = iio_priv(indio_dev);

	mutex_lock(&state->lock);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = max11108_read_single(indio_dev, val);
		if (ret == 0)
			ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_SCALE:
		vref_uv = regulator_get_voltage(state->vref_reg);
		if (vref_uv < 0) {
			dev_err(&indio_dev->dev,
				"dummy regulator \"get_voltage\" returns %d\n",
				vref_uv);
			ret = -EINVAL;
		}
		else {
			*val =  vref_uv / 1000;
			*val2 = MAX11108_LSB_DIV;
			ret = IIO_VAL_FRACTIONAL;
		}
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&state->lock);

	return ret;
}

static irqreturn_t max11108_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct max11108_state *state = iio_priv(indio_dev);
	u8 data[16] = { }; /* 12-bit ADC data + padding + 8 bytes timestamp */
	int ret;

	mutex_lock(&state->lock);

	/* Fill buffer with all channel */
	ret = max11108_read_single(indio_dev, (int*) data);
	if (ret < 0){
		dev_err(&indio_dev->dev, "channel read failure\n");
		goto trigger_handler_out;
	}
	iio_push_to_buffers_with_timestamp(indio_dev, data,
					   iio_get_time_ns(indio_dev));

trigger_handler_out:
	mutex_unlock(&state->lock);
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static const struct iio_trigger_ops max11108_trigger_ops = {
	.validate_device = &iio_trigger_validate_own_device,
};

static const struct iio_info max11108_info = {
	.read_raw = max11108_read_raw,
};

static int max11108_probe(struct spi_device *spi)
{
	int ret;
	struct iio_dev *indio_dev;
	struct max11108_state *state;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*state));
	if (!indio_dev) {
		dev_err(&indio_dev->dev, "iio_dev allocation failure\n");
		return -ENOMEM;
	}

	spi_set_drvdata(spi, indio_dev);

	state = iio_priv(indio_dev);
	state->spi = spi;

	mutex_init(&state->lock);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->dev.of_node = spi->dev.of_node;
	indio_dev->name = "max11108";
	indio_dev->info = &max11108_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = max11108_channels;
	indio_dev->num_channels = ARRAY_SIZE(max11108_channels);

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev,
					&iio_pollfunc_store_time,
					&max11108_trigger_handler,
					NULL);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Failed to setup buffer\n");
		return ret;
	}
	/* Create default trigger */
	state->trig = devm_iio_trigger_alloc(&spi->dev, "max11108-trigger");
	if (state->trig == NULL) {
		ret = -ENOMEM;
		dev_err(&indio_dev->dev, "Failed to allocate iio trigger\n");
		return ret;
	}
	state->trig->dev.parent = &spi->dev;
	state->trig->ops = &max11108_trigger_ops;
	iio_trigger_set_drvdata(state->trig, indio_dev);
	ret = iio_trigger_register(state->trig);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Triger registration failure.\n");
		return ret;
	}
	indio_dev->trig = iio_trigger_get(state->trig);

	state->vref_reg = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(state->vref_reg)) {
		dev_err(&indio_dev->dev, "Failed to locate vref regulator\n");
		return PTR_ERR(state->vref_reg);
	}

	ret = regulator_enable(state->vref_reg);
	if (ret) {
		dev_err(&indio_dev->dev, "Regulator enable failure\n");
		return ret;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&indio_dev->dev, "iio_dev registration failure\n");
		goto disable_regulator;
	}

	return 0;

disable_regulator:
	regulator_disable(state->vref_reg);

	return ret;
}

static int max11108_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct max11108_state *state = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	regulator_disable(state->vref_reg);

	return 0;
}

static const struct of_device_id max11108_ids[] = {
	{.compatible = "maxim,max11108"},
	{ },
};
MODULE_DEVICE_TABLE(of, max11108_ids);

static struct spi_driver max11108_driver = {
	.driver = {
		.name	= "max11108",
		.of_match_table = of_match_ptr(max11108_ids),
	},
	.probe		= max11108_probe,
	.remove		= max11108_remove,
};

module_spi_driver(max11108_driver);

MODULE_AUTHOR("Uri Mashiach <uri.mashiach@compulab.co.il>");
MODULE_DESCRIPTION("Maxim max11108 ADC Driver");
MODULE_LICENSE("GPL v2");
