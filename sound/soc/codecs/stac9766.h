/*
 * stac9766.h  --  STAC9766 Soc Audio driver
 */

#ifndef _STAC9766_H
#define _STAC9766_H

#define AC97_STAC_ANALOG_SPECIAL 0x6E
#define AC97_STAC_STEREO_MIC 0x78

/* STAC9766 DAI ID's */
#define STAC9766_DAI_AC97_ANALOG		0
#define STAC9766_DAI_AC97_DIGITAL		1

extern struct snd_soc_dai stac9766_dai[];

#endif
