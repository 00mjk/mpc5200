/*
 * linux/sound/mpc5200-ac97.c -- AC97 support for the Freescale MPC52xx chip.
 *
 * Copyright 2008 Jon Smirl, Digispeaker
 * Author: Jon Smirl <jonsmirl@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define DEBUG

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/soc-of-simple.h>

#include <sysdev/bestcomm/bestcomm.h>
#include <sysdev/bestcomm/gen_bd.h>
#include <asm/time.h>
#include <asm/mpc52xx.h>
#include <asm/mpc52xx_psc.h>

#include "mpc5200_dma.h"
#include "mpc5200_psc_ac97.h"

MODULE_AUTHOR("Jon Smirl <jonsmirl@gmail.com>");
MODULE_DESCRIPTION("mpc5200 AC97 module");
MODULE_LICENSE("GPL");


/**
 * PSC_AC97_RATES: sample rates supported by the AC97
 *
 * This driver currently only supports the PSC running in AC97 slave mode,
 * which means the codec determines the sample rate.  Therefore, we tell
 * ALSA that we support all rates and let the codec driver decide what rates
 * are really supported.
 */
#define PSC_AC97_RATES (SNDRV_PCM_RATE_5512 | SNDRV_PCM_RATE_8000_192000 | \
			SNDRV_PCM_RATE_CONTINUOUS)

/**
 * PSC_AC97_FORMATS: audio formats supported by the PSC AC97 mode
 */
#define PSC_AC97_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_BE | \
			 SNDRV_PCM_FMTBIT_S24_BE | SNDRV_PCM_FMTBIT_S24_BE | \
			 SNDRV_PCM_FMTBIT_S32_BE)

#define DRV_NAME "mpc5200-psc-ac97"

struct psc_dma *psc_dma;

static unsigned short psc_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	//struct psc_dma *psc_dma = ac97->private_data;
	int timeout;
	unsigned int val;

	spin_lock(&psc_dma->lock);
	//printk("ac97 read: reg %04x\n", reg);

	/* Wait for it to be ready */
	timeout = 1000;
	while ((--timeout) && (in_be16(&psc_dma->psc_regs->sr_csr.status) &
						MPC52xx_PSC_SR_CMDSEND) )
		udelay(10);

	if (!timeout) {
		printk(KERN_ERR DRV_NAME ": timeout on ac97 bus (rdy)\n");
		return 0xffff;
	}

	/* Do the read */
	out_be32(&psc_dma->psc_regs->ac97_cmd, (1<<31) | ((reg & 0x7f) << 24));

	/* Wait for the answer */
	timeout = 1000;
	while ((--timeout) && !(in_be16(&psc_dma->psc_regs->sr_csr.status) &
						MPC52xx_PSC_SR_DATA_VAL) )
		udelay(10);

	if (!timeout) {
		printk(KERN_ERR DRV_NAME ": timeout on ac97 read (val) %x\n", in_be16(&psc_dma->psc_regs->sr_csr.status));
		return 0xffff;
	}

	/* Get the data */
	val = in_be32(&psc_dma->psc_regs->ac97_data);
	if ( ((val>>24) & 0x7f) != reg ) {
		printk(KERN_ERR DRV_NAME ": reg echo error on ac97 read\n");
		return 0xffff;
	}
	val = (val >> 8) & 0xffff;

	//printk("ac97 read ok: reg %04x  val %04x\n", reg, val);

	spin_unlock(&psc_dma->lock);
	return (unsigned short) val;
}

static void psc_ac97_write(struct snd_ac97 *ac97, unsigned short reg, unsigned short val)
{
	//struct psc_dma *psc_dma = ac97->private_data;
	int timeout;

	//printk("ac97 write: reg %04x  val %04x\n", reg, val);
	spin_lock(&psc_dma->lock);

	/* Wait for it to be ready */
	timeout = 1000;
	while ((--timeout) && (in_be16(&psc_dma->psc_regs->sr_csr.status) &
						MPC52xx_PSC_SR_CMDSEND) )
		udelay(10);

	if (!timeout) {
		printk(KERN_ERR DRV_NAME ": timeout on ac97 write\n");
		return;
	}

	/* Write data */
	out_be32(&psc_dma->psc_regs->ac97_cmd, ((reg & 0x7f) << 24) | (val << 8));

	spin_unlock(&psc_dma->lock);
}

static void psc_ac97_cold_reset(struct snd_ac97 *ac97)
{
	//struct psc_dma *psc_dma = ac97->private_data;

	printk("psc_ac97_cold_reset %p\n", ac97);

	/* Do a cold reset */
	out_8(&psc_dma->psc_regs->op1, MPC52xx_PSC_OP_RES);
	udelay(10);
	out_8(&psc_dma->psc_regs->op0, MPC52xx_PSC_OP_RES);
	udelay(50);

	/* PSC recover from cold reset (cfr user manual, not sure if useful) */
	out_be32(&psc_dma->psc_regs->sicr, in_be32(&psc_dma->psc_regs->sicr));
}

static void psc_ac97_warm_reset(struct snd_ac97 *ac97)
{
	printk("psc_ac97_warm_reset\n");
}

struct snd_ac97_bus_ops soc_ac97_ops = {
	.read		= psc_ac97_read,
	.write		= psc_ac97_write,
	.reset		= psc_ac97_cold_reset,
	.warm_reset	= psc_ac97_warm_reset,
};
EXPORT_SYMBOL_GPL(soc_ac97_ops);

#ifdef CONFIG_PM
static int psc_ac97_suspend(struct snd_soc_dai *dai)
{
	return 0;
}

static int psc_ac97_resume(struct snd_soc_dai *dai)
{
	return 0;
}

#else
#define psc_ac97_suspend	NULL
#define psc_ac97_resume	NULL
#endif

static int psc_ac97_hw_analog_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = rtd->dai->cpu_dai->private_data;

	dev_dbg(psc_dma->dev, "%s(substream=%p) p_size=%i p_bytes=%i"
		" periods=%i buffer_size=%i  buffer_bytes=%i channels=%i\n",
		__func__, substream, params_period_size(params),
		params_period_bytes(params), params_periods(params),
		params_buffer_size(params), params_buffer_bytes(params),
		params_channels(params));

	// FIXME, need a spinlock to protect access
	if (params_channels(params) == 1)
		out_be32(&psc_dma->psc_regs->ac97_slots, 0x01000000);
	else
		out_be32(&psc_dma->psc_regs->ac97_slots, 0x03000000);

  	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int psc_ac97_hw_digital_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	return 0;
}

/**
 * psc_ac97_set_fmt: set the serial format.
 *
 * This function is called by the machine driver to tell us what serial
 * format to use.
 *
 * This driver only supports AC97 mode.  Return an error if the format is
 * not SND_SOC_DAIFMT_AC97.
 *
 * @format: one of SND_SOC_DAIFMT_xxx
 */
static int psc_ac97_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int format)
{
	struct psc_dma *psc_dma = cpu_dai->private_data;
	dev_dbg(psc_dma->dev, "psc_ac97_set_fmt(cpu_dai=%p, format=%i)\n",
				cpu_dai, format);
	return (format == SND_SOC_DAIFMT_AC97) ? 0 : -EINVAL;
}

/* ---------------------------------------------------------------------
 * ALSA SoC Bindings
 *
 * - Digital Audio Interface (DAI) template
 * - create/destroy dai hooks
 */

/**
 * psc_ac97_dai_template: template CPU Digital Audio Interface
 */
static struct snd_soc_dai_ops psc_ac97_analog_ops = {
	.startup	= mpc5200_dma_startup,
	.hw_params	= psc_ac97_hw_analog_params,
	.hw_free	= mpc5200_dma_hw_free,
	.shutdown	= mpc5200_dma_shutdown,
	.trigger	= mpc5200_dma_trigger,
	.set_fmt	= psc_ac97_set_fmt,
};

static struct snd_soc_dai_ops psc_ac97_digital_ops = {
	.startup	= mpc5200_dma_startup,
	.hw_params	= psc_ac97_hw_digital_params,
	.hw_free	= mpc5200_dma_hw_free,
	.shutdown	= mpc5200_dma_shutdown,
	.trigger	= mpc5200_dma_trigger,
	.set_fmt	= psc_ac97_set_fmt,
};

static struct snd_soc_dai psc_ac97_dai_template[] = {
{
	.name	= "%s analog",
	.suspend = psc_ac97_suspend,
	.resume = psc_ac97_resume,
	.playback = {
		.channels_min	= 1,
		.channels_max	= 6,
		.rates		= SNDRV_PCM_RATE_8000_48000,
		.formats	= SNDRV_PCM_FORMAT_S32_BE,
	},
	.capture = {
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_8000_48000,
		.formats	= SNDRV_PCM_FMTBIT_S32_BE,
	},
	.ops = &psc_ac97_analog_ops,
},
{
	.name	= "%s digital",
	.playback = {
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_32000 | \
			SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
		.formats	= SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE,
	},
	.ops = &psc_ac97_digital_ops,
}};

/* ---------------------------------------------------------------------
 * Sysfs attributes for debugging
 */

static ssize_t psc_ac97_status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct psc_dma *psc_dma = dev_get_drvdata(dev);

	return sprintf(buf, "status=%.4x sicr=%.8x rfnum=%i rfstat=0x%.4x "
			"tfnum=%i tfstat=0x%.4x\n",
			in_be16(&psc_dma->psc_regs->sr_csr.status),
			in_be32(&psc_dma->psc_regs->sicr),
			in_be16(&psc_dma->fifo_regs->rfnum) & 0x1ff,
			in_be16(&psc_dma->fifo_regs->rfstat),
			in_be16(&psc_dma->fifo_regs->tfnum) & 0x1ff,
			in_be16(&psc_dma->fifo_regs->tfstat));
}

static int *psc_ac97_get_stat_attr(struct psc_dma *psc_dma, const char *name)
{
	if (strcmp(name, "playback_underrun") == 0)
		return &psc_dma->stats.underrun_count;
	if (strcmp(name, "capture_overrun") == 0)
		return &psc_dma->stats.overrun_count;

	return NULL;
}

static ssize_t psc_ac97_stat_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct psc_dma *psc_dma = dev_get_drvdata(dev);
	int *attrib;

	attrib = psc_ac97_get_stat_attr(psc_dma, attr->attr.name);
	if (!attrib)
		return 0;

	return sprintf(buf, "%i\n", *attrib);
}

static ssize_t psc_ac97_stat_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t count)
{
	struct psc_dma *psc_dma = dev_get_drvdata(dev);
	int *attrib;

	attrib = psc_ac97_get_stat_attr(psc_dma, attr->attr.name);
	if (!attrib)
		return 0;

	*attrib = simple_strtoul(buf, NULL, 0);
	return count;
}

static DEVICE_ATTR(status, 0644, psc_ac97_status_show, NULL);
static DEVICE_ATTR(playback_underrun, 0644, psc_ac97_stat_show,
			psc_ac97_stat_store);
static DEVICE_ATTR(capture_overrun, 0644, psc_ac97_stat_show,
			psc_ac97_stat_store);


/* ---------------------------------------------------------------------
 * OF platform bus binding code:
 * - Probe/remove operations
 * - OF device match table
 */
static int __devinit psc_ac97_of_probe(struct of_device *op,
				      const struct of_device_id *match)
{
	phys_addr_t fifo;
	//struct psc_dma *psc_dma;
	struct resource res;
	int i, nDAI, size, psc_id, irq, rc;
	const __be32 *prop;
	void __iomem *regs;

	/* Get the PSC ID */
	prop = of_get_property(op->node, "cell-index", &size);
	if (!prop || size < sizeof *prop)
		return -ENODEV;
	psc_id = be32_to_cpu(*prop);

	/* Fetch the registers and IRQ of the PSC */
	irq = irq_of_parse_and_map(op->node, 0);
	if (of_address_to_resource(op->node, 0, &res)) {
		dev_err(&op->dev, "Missing reg property\n");
		return -ENODEV;
	}
	regs = ioremap(res.start, 1 + res.end - res.start);
	if (!regs) {
		dev_err(&op->dev, "Could not map registers\n");
		return -ENODEV;
	}

	/* Allocate and initialize the driver private data */
	psc_dma = kzalloc(sizeof *psc_dma, GFP_KERNEL);
	if (!psc_dma) {
		iounmap(regs);
		return -ENOMEM;
	}
	printk("psc_dma %p\n", psc_dma);

	spin_lock_init(&psc_dma->lock);
	psc_dma->irq = irq;
	psc_dma->psc_regs = regs;
	psc_dma->fifo_regs = regs + sizeof *psc_dma->psc_regs;
	psc_dma->dev = &op->dev;
	psc_dma->playback.psc_dma = psc_dma;
	psc_dma->capture.psc_dma = psc_dma;
	snprintf(psc_dma->name, sizeof psc_dma->name, "PSC%u AC97", psc_id+1);

	/* Fill out the CPU DAI structure */
	nDAI = ARRAY_SIZE(psc_ac97_dai_template);
	nDAI = min(nDAI, SOC_OF_SIMPLE_MAX_DAI);
	for (i = 0; i < nDAI; i++) {
		memcpy(&psc_dma->dai[i], &psc_ac97_dai_template[i], sizeof(struct snd_soc_dai));
		psc_dma->dai[i].private_data = psc_dma;
		snprintf(psc_dma->stream_name[i], PSC_STREAM_NAME_LEN, psc_ac97_dai_template[i].name, psc_dma->name);
		psc_dma->dai[i].name = psc_dma->stream_name[i];
		psc_dma->dai[i].id = psc_id;
	}

	/* Find the address of the fifo data registers and setup the
	 * DMA tasks */
	fifo = res.start + offsetof(struct mpc52xx_psc, buffer.buffer_32);
	psc_dma->capture.bcom_task =
		bcom_psc_gen_bd_rx_init(psc_id, 10, fifo, 512);
	psc_dma->playback.bcom_task =
		bcom_psc_gen_bd_tx_init(psc_id, 10, fifo);
	if (!psc_dma->capture.bcom_task ||
	    !psc_dma->playback.bcom_task) {
		dev_err(&op->dev, "Could not allocate bestcomm tasks\n");
		iounmap(regs);
		kfree(psc_dma);
		return -ENODEV;
	}

	/* Disable all interrupts and reset the PSC */
	out_be16(&psc_dma->psc_regs->isr_imr.imr, 0);
	out_8(&psc_dma->psc_regs->command, MPC52xx_PSC_RST_RX); /* reset receiver */
	out_8(&psc_dma->psc_regs->command, MPC52xx_PSC_RST_TX); /* reset transmitter */
	out_8(&psc_dma->psc_regs->command, MPC52xx_PSC_RST_ERR_STAT); /* reset error */
	out_8(&psc_dma->psc_regs->command, MPC52xx_PSC_SEL_MODE_REG_1); /* reset mode */

	/* Do a cold reset of codec */
	out_8(&psc_dma->psc_regs->op1, MPC52xx_PSC_OP_RES);
	udelay(10);
	out_8(&psc_dma->psc_regs->op0, MPC52xx_PSC_OP_RES);
	udelay(50);

	/* Configure the serial interface mode to AC97 */
	psc_dma->sicr = MPC52xx_PSC_SICR_SIM_AC97 | MPC52xx_PSC_SICR_ENAC97;
	out_be32(&psc_dma->psc_regs->sicr, psc_dma->sicr);

	/* No slots active */
	out_be32(&psc_dma->psc_regs->ac97_slots, 0x00000000);

	/* Set up mode register;
	 * First write: RxRdy (FIFO Alarm) generates rx FIFO irq
	 * Second write: register Normal mode for non loopback
	 */
	out_8(&psc_dma->psc_regs->mode, 0);
	out_8(&psc_dma->psc_regs->mode, 0);

	/* Set the TX and RX fifo alarm thresholds */
	out_be16(&psc_dma->fifo_regs->rfalarm, 0x100);
	out_8(&psc_dma->fifo_regs->rfcntl, 0x4);
	out_be16(&psc_dma->fifo_regs->tfalarm, 0x100);
	out_8(&psc_dma->fifo_regs->tfcntl, 0x7);

	/* Go */
	out_8(&psc_dma->psc_regs->command, MPC52xx_PSC_TX_ENABLE);
	out_8(&psc_dma->psc_regs->command, MPC52xx_PSC_RX_ENABLE);

	/* Lookup the IRQ numbers */
	psc_dma->playback.irq =
		bcom_get_task_irq(psc_dma->playback.bcom_task);
	psc_dma->capture.irq =
		bcom_get_task_irq(psc_dma->capture.bcom_task);

	/* Save what we've done so it can be found again later */
	dev_set_drvdata(&op->dev, psc_dma);

	/* Register the SYSFS files */
	rc = device_create_file(psc_dma->dev, &dev_attr_status);
	rc |= device_create_file(psc_dma->dev, &dev_attr_capture_overrun);
	rc |= device_create_file(psc_dma->dev, &dev_attr_playback_underrun);
	if (rc)
		dev_info(psc_dma->dev, "error creating sysfs files\n");

	rc = snd_soc_register_dais(psc_dma->dai, nDAI);
	if (rc != 0) {
		printk("Failed to register DAI\n");
		return 0;
	}

	/* Tell the ASoC OF helpers about it */
	of_snd_soc_register_cpu_dai(op->node, psc_dma->dai, nDAI);

	return 0;
}

static int __devexit psc_ac97_of_remove(struct of_device *op)
{
	struct psc_dma *psc_dma = dev_get_drvdata(&op->dev);

	dev_dbg(&op->dev, "psc_ac97_remove()\n");

	bcom_gen_bd_rx_release(psc_dma->capture.bcom_task);
	bcom_gen_bd_tx_release(psc_dma->playback.bcom_task);

	iounmap(psc_dma->psc_regs);
	iounmap(psc_dma->fifo_regs);
	kfree(psc_dma);
	dev_set_drvdata(&op->dev, NULL);

	return 0;
}

/* Match table for of_platform binding */
static struct of_device_id psc_ac97_match[] __devinitdata = {
	{ .compatible = "fsl,mpc5200-psc-ac97", },
	{}
};
MODULE_DEVICE_TABLE(of, psc_ac97_match);

static struct of_platform_driver psc_ac97_driver = {
	.match_table = psc_ac97_match,
	.probe = psc_ac97_of_probe,
	.remove = __devexit_p(psc_ac97_of_remove),
	.driver = {
		.name = "mpc5200-psc-ac97",
		.owner = THIS_MODULE,
	},
};

/* ---------------------------------------------------------------------
 * Module setup and teardown; simply register the of_platform driver
 * for the PSC in AC97 mode.
 */
static int __init psc_ac97_init(void)
{
	return of_register_platform_driver(&psc_ac97_driver);
}
module_init(psc_ac97_init);

static void __exit psc_ac97_exit(void)
{
	of_unregister_platform_driver(&psc_ac97_driver);
}
module_exit(psc_ac97_exit);
