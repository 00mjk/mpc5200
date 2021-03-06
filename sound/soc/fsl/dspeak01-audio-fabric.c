/*
 * sound/soc/fsl/dspeak01_fabric.c -- The ALSA glue fabric for Digispeaker dspeak01
 *
 * Copyright 2008 Jon Smirl, Digispeaker
 * Author: Jon Smirl <jonsmirl@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_i2c.h>
#include <linux/i2c/max9485.h>

#include <sound/soc.h>
#include <sound/soc-of-simple.h>

#include "../codecs/tas5504.h"
#include "mpc5200_dma.h"
#include "mpc5200_psc_i2s.h"

static struct dspeak01_fabric {
	struct i2c_client *clock;
} fabric;

static int dspeak01_fabric_startup(struct snd_pcm_substream *substream)
{
	printk("dspeak01_fabric_startup\n");
	return 0;
}

static void dspeak01_fabric_shutdown(struct snd_pcm_substream *substream)
{
	printk("dspeak01_fabric_shutdown\n");
}

static int dspeak01_fabric_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	uint rate, select;
	int ret;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
    struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;

	printk("dspeak01_fabric_hw_params\n");

	switch (params_rate(params)) {
	case 11025:
	case 22050:
	case 44100:
	case 88200:
	case 176400:
		rate = 22579200;
		select = MAX9485_225792;
		break;
	default:
		rate = 24576000;
		select = MAX9485_245760;
		break;
	}
	max9485_set(fabric.clock, select | MAX9485_CLK_OUT_2);

	ret = snd_soc_dai_set_sysclk(cpu_dai, MPC52xx_CLK_CELLSLAVE, rate, SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	return 0;
}

static int dspeak01_fabric_hw_free(struct snd_pcm_substream *substream)
{
	printk("dspeak01_fabric_hw_free\n");
	return 0;
}

static int dspeak01_fabric_prepare(struct snd_pcm_substream *substream)
{
	printk("dspeak01_fabric_prepare\n");
	return 0;
}

static int dspeak01_fabric_trigger(struct snd_pcm_substream *substream, int trigger)
{
	printk("dspeak01_fabric_trigger\n");
	return 0;
}

static struct snd_soc_ops dspeak01_fabric_ops = {
	.startup = dspeak01_fabric_startup,
	.shutdown = dspeak01_fabric_shutdown,
	.hw_params = dspeak01_fabric_hw_params,
	.hw_free = dspeak01_fabric_hw_free,
	.prepare = dspeak01_fabric_prepare,
	.trigger = dspeak01_fabric_trigger,
};

static int __devinit dspeak01_fabric_probe(struct of_device *op,
				      const struct of_device_id *match)
{
	const phandle *handle;
	struct device_node *clock_node;
	unsigned int len;

	handle = of_get_property(op->node, "clock-handle", &len);
	if (!handle || len < sizeof(handle))
		return -ENODEV;

	clock_node = of_find_node_by_phandle(*handle);
	if (!clock_node)
		return -ENODEV;

	fabric.clock = of_find_i2c_device_by_node(clock_node);
	if (!fabric.clock)
		return -ENODEV;

	return 0;
}

static int __exit dspeak01_fabric_remove(struct of_device *op)
{
	put_device(&fabric.clock->dev);
	return 0;
}


static struct snd_soc_device device;
static struct snd_soc_card card;

static struct snd_soc_dai_link dspeak01_fabric_dai[] = {
{
	.name = "I2S",
	.stream_name = "I2S Out",
	.codec_dai = &tas5504_dai,
	.cpu_dai = psc_i2s_dai,
},
};

static __init int dspeak01_fabric_init(void)
{
	struct platform_device *pdev;
	int rc;

	if (!machine_is_compatible("digispeaker,dspeak01"))
		return -ENODEV;

	card.platform = &mpc5200_audio_dma_platform;
	card.name = "Efika";
	card.dai_link = dspeak01_fabric_dai;
	card.num_links = ARRAY_SIZE(dspeak01_fabric_dai);

	device.card = &card;
	device.codec_dev = &tas5504_soc_codec_dev;

	pdev = platform_device_alloc("soc-audio", 1);
	if (!pdev) {
		pr_err("dspeak01_fabric_init: platform_device_alloc() failed\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, &device);
	device.dev = &pdev->dev;

	rc = platform_device_add(pdev);
	if (rc) {
		pr_err("dspeak01_fabric_init: platform_device_add() failed\n");
		return -ENODEV;
	}
	return 0;
}

static __exit void dspeak01_fabric_exit(void)
{
}

module_init(dspeak01_fabric_init);
module_exit(dspeak01_fabric_exit);


/* Module information */
MODULE_AUTHOR("Jon Smirl");
MODULE_DESCRIPTION("ASOC Digispeaker fabric module");
MODULE_LICENSE("GPL");
