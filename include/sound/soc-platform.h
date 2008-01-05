/*
 * linux/sound/soc-platform.h -- ALSA SoC Layer
 *
 * Author:		Liam Girdwood
 * Created:		Aug 11th 2005
 * Copyright:	Wolfson Microelectronics. PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * 
 * ALSA SoC platform interface. A platform is usually a CPU audio subsystem 
 * with DMA and DAI. However, it can also represent a GSM processor or BT
 * interface. 
 */

#ifndef __LINUX_SND_SOC_PLATFORM_H
#define __LINUX_SND_SOC_PLATFORM_H

#include <linux/types.h>
#include <linux/workqueue.h>

struct snd_soc_platform;
struct snd_soc_machine;
struct snd_soc_dai;
struct snd_pcm_ops;
struct snd_card;

/**
 * snd_soc_platform_add_dai - add DAI to platform.
 * @platform: platform
 * @dai: pointer to DAI
 * @num: number of DAI to add
 *
 * Adds <num> Digital Audio Interfaces to platform.
 */
int snd_soc_platform_add_dai(struct snd_soc_platform *platform, 
	struct snd_soc_dai *dai, int num);

/**
 * snd_soc_register_platform - register ASoC platform driver.
 * @platform: platform driver
 *
 * Registers a platform driver with ASoC core.
 */
int snd_soc_register_platform(struct snd_soc_platform *platform);

/**
 * snd_soc_unregister_platform - unregister ASoC platform driver.
 * @platform: platform driver
 *
 * Unregisters a platform driver with ASoC core.
 */
void snd_soc_unregister_platform(struct snd_soc_platform *platform);

/**
 * snd_soc_set_runtime_hwparams - register hw params
 * @substream: parent machine
 * @hw: platform ID name
 *
 * Registers a PCM's hardware runtime parameters with core.
 */
int snd_soc_set_runtime_hwparams(struct snd_pcm_substream *substream,
	const struct snd_pcm_hardware *hw);

/* 
 * ASoC platform interface 
 *
 * The platform interface is used to describe a CPU audio interface. This
 * includes the audio DMA engine and DAI.
 * 
 * The platform interface can also be used to represent a GSM or BT DAI. 
 */
struct snd_soc_platform {
	struct device dev;
	char *name;
	
	/* runtime */
	struct mutex mutex;
	struct list_head list;
	struct list_head dai_list;
	struct snd_soc_machine *machine;
	
	/* platform ALSA ops - optional */
	const struct snd_pcm_ops *pcm_ops;
	
	/* pcm creation and destruction */
	int (*pcm_new)(struct snd_soc_platform *platform, 
		struct snd_card *card, int playback, int capture, 
		struct snd_pcm *pcm);
	void (*pcm_free)(struct snd_pcm *pcm);
	
	void *private_data;
};
#define to_snd_soc_platform(d) \
	container_of(d, struct snd_soc_platform, dev)

#endif
