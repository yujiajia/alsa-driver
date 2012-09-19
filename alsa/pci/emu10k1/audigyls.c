/*
 *  Copyright (c) by James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver AUDIGYLS chips
 *
 *  FEATURES currently supported:
 *    Front, Rear and Center/LFE.
 *    Surround40 and Surround51.
 *    Capture from MIC input.
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    Need to add a way to select capture source.
 *    4 Capture channels, only one implemented so far.
 *    SPDIF playback not implemented.
 *    Need to find out what the AC97 chip actually does.
 *    Other rates apart from 48khz not implemented.
 *    MIDI
 *    --
 *  GENERAL INFO:
 *    P17 Chip: CA0106-DAT
 *    AC97 Codec: STAC 9721
 *    ADC: Philips 1361T (Stereo 24bit)
 *    DAC: WM8746EDS (6-channel, 24bit, 192Khz)
 *
 *  This code was initally based on code from ALSA's emu10k1x.c which is:
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <sound/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/info.h>

MODULE_AUTHOR("James Courtier-Dutton <James@superbug.demon.co.uk>");
MODULE_DESCRIPTION("AUDIGYLS");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Creative SB Audigy LS}");

// module parameters (see "Module Parameters")
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
static int boot_devs;

module_param_array(index, int, boot_devs, 0444);
MODULE_PARM_DESC(index, "Index value for the AUDIGYLS soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
module_param_array(id, charp, boot_devs, 0444);
MODULE_PARM_DESC(id, "ID string for the AUDIGYLS soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
module_param_array(enable, bool, boot_devs, 0444);
MODULE_PARM_DESC(enable, "Enable the AUDIGYLS soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);


/************************************************************************************************/
/* PCI function 0 registers, address = <val> + PCIBASE0						*/
/************************************************************************************************/

#define PTR			0x00		/* Indexed register set pointer register	*/
						/* NOTE: The CHANNELNUM and ADDRESS words can	*/
						/* be modified independently of each other.	*/

#define DATA			0x04		/* Indexed register set data register		*/

#define IPR			0x08		/* Global interrupt pending register		*/
						/* Clear pending interrupts by writing a 1 to	*/
						/* the relevant bits and zero to the other bits	*/
#define IPR_CH_0_LOOP           0x00000800      /* Channel 0 loop                               */
#define IPR_CH_0_HALF_LOOP      0x00000100      /* Channel 0 half loop                          */

#define INTE			0x0c		/* Interrupt enable register			*/
#define INTE_CH_0_LOOP          0x00000800      /* Channel 0 loop                               */
#define INTE_CH_0_HALF_LOOP     0x00000100      /* Channel 0 half loop                          */

#define HCFG			0x14		/* Hardware config register			*/

#define HCFG_LOCKSOUNDCACHE	0x00000008	/* 1 = Cancel bustmaster accesses to soundcache */
						/* NOTE: This should generally never be used.  	*/
#define HCFG_AUDIOENABLE	0x00000001	/* 0 = CODECs transmit zero-valued samples	*/
						/* Should be set to 1 when the EMU10K1 is	*/
						/* completely initialized.			*/

/********************************************************************************************************/
/* Audigy LS pointer-offset register set, accessed through the PTR and DATA registers                     */
/********************************************************************************************************/
                                                                                                                           
#define PLAYBACK_UNKNOWN0	0x00		/* Something used in playback */
#define PLAYBACK_UNKNOWN1	0x01		/* Something used in playback */
#define PLAYBACK_UNKNOWN2	0x02		/* Something used in playback */
#define PLAYBACK_UNKNOWN3	0x03		/* Something ?? */
#define PLAYBACK_DMA_ADDR	0x04		/* Something used in playback */
#define PLAYBACK_BUFFER_SIZE	0x05		/* Something used in playback */
#define PLAYBACK_POINTER	0x06		/* Playback buffer pointer. Sample currently in DAC */
#define PLAYBACK_UNKNOWN7	0x07		/* Something used in playback */
#define PLAYBACK_UNKNOWN8	0x08		/* Something used in playback */
#define PLAYBACK_UNKNOWN9	0x09		/* Something ?? */
#define CAPTURE_DMA_ADDR	0x10		/* Something used in capture */
#define CAPTURE_BUFFER_SIZE	0x11		/* Something used in capture */
#define CAPTURE_POINTER		0x12		/* Capture buffer pointer. Sample currently in ADC */
#define CAPTURE_UNKNOWN13	0x13		/* Something used in capture */
#define AC97DATA		0x1c		/* AC97 register set data register (16 bit)	*/

#define AC97ADDRESS		0x1e		/* AC97 register set address register (8 bit)	*/
#define PLAYBACk_LAST_SAMPLE    0x20            /* The sample currently being played */
#define BASIC_INTERRUPT         0x40            /* Used by both playback and capture interrupt handler */
#define SPCS0			0x41		/* SPDIF output Channel Status 0 register default=0x02108004, non-audio=0x02108006	*/
#define SPCS_CLKACCYMASK	0x30000000	/* Clock accuracy				*/
#define SPCS_CLKACCY_1000PPM	0x00000000	/* 1000 parts per million			*/
#define SPCS_CLKACCY_50PPM	0x10000000	/* 50 parts per million				*/
#define SPCS_CLKACCY_VARIABLE	0x20000000	/* Variable accuracy				*/
#define SPCS_SAMPLERATEMASK	0x0f000000	/* Sample rate					*/
#define SPCS_SAMPLERATE_44	0x00000000	/* 44.1kHz sample rate				*/
#define SPCS_SAMPLERATE_48	0x02000000	/* 48kHz sample rate				*/
#define SPCS_SAMPLERATE_32	0x03000000	/* 32kHz sample rate				*/
#define SPCS_CHANNELNUMMASK	0x00f00000	/* Channel number				*/
#define SPCS_CHANNELNUM_UNSPEC	0x00000000	/* Unspecified channel number			*/
#define SPCS_CHANNELNUM_LEFT	0x00100000	/* Left channel					*/
#define SPCS_CHANNELNUM_RIGHT	0x00200000	/* Right channel				*/
#define SPCS_SOURCENUMMASK	0x000f0000	/* Source number				*/
#define SPCS_SOURCENUM_UNSPEC	0x00000000	/* Unspecified source number			*/
#define SPCS_GENERATIONSTATUS	0x00008000	/* Originality flag (see IEC-958 spec)		*/
#define SPCS_CATEGORYCODEMASK	0x00007f00	/* Category code (see IEC-958 spec)		*/
#define SPCS_MODEMASK		0x000000c0	/* Mode (see IEC-958 spec)			*/
#define SPCS_EMPHASISMASK	0x00000038	/* Emphasis					*/
#define SPCS_EMPHASIS_NONE	0x00000000	/* No emphasis					*/
#define SPCS_EMPHASIS_50_15	0x00000008	/* 50/15 usec 2 channel				*/
#define SPCS_COPYRIGHT		0x00000004	/* Copyright asserted flag -- do not modify	*/
#define SPCS_NOTAUDIODATA	0x00000002	/* 0 = Digital audio, 1 = not audio		*/
#define SPCS_PROFESSIONAL	0x00000001	/* 0 = Consumer (IEC-958), 1 = pro (AES3-1992)	*/

#define SPDIF_SELECT		0x45		/* Enables SPDIF or Analogue outputs 0-Analogue, 0xf00-SPDIF */
#define CAPTURE_SOURCE          0x60            /* Capture Source 0 = MIC */
#define CAPTURE_SOURCE_CHANNEL0 0xf0000000	/* Mask for selecting the Capture sources */
#define CAPTURE_SOURCE_CHANNEL1 0x0f000000	/* 0 - MIC input. */
#define CAPTURE_SOURCE_CHANNEL2 0x00f00000      /* 1 - ??, 2-??, 3-?? */
#define CAPTURE_SOURCE_CHANNEL3 0x000f0000
#define CAPTURE_SOURCE_LOOPBACK 0x0000ffff

#define CAPTURE_VOLUME1         0x61            /* Capture  volume per voice */
#define CAPTURE_VOLUME2         0x62            /* Capture  volume per voice */
#define UNKNOWN_VOLUME          0x66            /* Unknown  volume per voice */
#define PLAYBACK_VOLUME         0x6a            /* Playback volume per voice */
#define UART1                   0x6c            /* Uart, used in setting sample rates, bits per sample etc. */
#define UART2                   0x6d            /* Uart, used in setting sample rates, bits per sample etc. */
#define ROUTING1                0x71            /* Some sort of routing. default = 0xf0000000 */
#define ROUTING2                0x72            /* Some sort of routing. default = 0x0f0f003f */
#define CHIP_VERSION            0x74            /* P17 Chip version */
#define EXTENDED_INT_MASK       0x75            /* Used by both playback and capture interrupt handler */
						/* Sets which Interrupts are enabled. */
#define EXTENDED_INT            0x76            /* Used by both playback and capture interrupt handler */
						/* Shows which interrupts are active at the moment. */
#define EXTENDED_INT_TIMER      0x79            /* Used by both playback and capture interrupt handler */
						/* Causes interrupts based on timer intervals. */
#define SET_CHANNEL 0  /* Testing channel outputs 0=Front, 1=Center/LFE, 2=Unknown, 3=Rear */
#define PCM_FRONT_CHANNEL 0
#define PCM_CENTER_LFE_CHANNEL 1
#define PCM_UNKNOWN_CHANNEL 2
#define PCM_REAR_CHANNEL 3

#define chip_t audigyls_t

typedef struct snd_audigyls_voice audigyls_voice_t;
typedef struct snd_audigyls_channel audigyls_channel_t;
typedef struct snd_audigyls audigyls_t;
typedef struct snd_audigyls_pcm audigyls_pcm_t;

struct snd_audigyls_voice {
	audigyls_t *emu;
	int number;
	int use;
	void (*interrupt)(audigyls_t *emu, audigyls_voice_t *pvoice);
  
	audigyls_pcm_t *epcm;
};

struct snd_audigyls_channel {
	audigyls_t *emu;
	int number;
	int use;
	void (*interrupt)(audigyls_t *emu, audigyls_channel_t *channel);
	audigyls_pcm_t *epcm;
};


struct snd_audigyls_pcm {
	audigyls_t *emu;
	snd_pcm_substream_t *substream;
	audigyls_voice_t *voice;
        int channel_id;
	unsigned short running;
};

// definition of the chip-specific record
struct snd_audigyls {
	snd_card_t *card;
	struct pci_dev *pci;

	unsigned long port;
	struct resource *res_port;
	int irq;

	unsigned int revision;		/* chip revision */
	unsigned int serial;            /* serial number */
	unsigned short model;		/* subsystem id */

	spinlock_t emu_lock;
	spinlock_t voice_lock;

	ac97_t *ac97;
	snd_pcm_t *pcm;

	audigyls_voice_t voices[4];
	audigyls_channel_t channels[4];
	audigyls_channel_t capture_channel;

	struct snd_dma_device dma_dev;
	struct snd_dma_buffer buffer;
        snd_kcontrol_t *ctl_front_volume;
};

#define audigyls_t_magic        0xa15a4501
#define audigyls_pcm_t_magic	0xa15a4502

/* hardware definition */
static snd_pcm_hardware_t snd_audigyls_playback_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | 
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		6,
	.buffer_bytes_max =	(32*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(16*1024),
	.periods_min =		2,
	.periods_max =		16,
	.fifo_size =		0,
};

static snd_pcm_hardware_t snd_audigyls_capture_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | 
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(32*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(16*1024),
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

static unsigned int snd_audigyls_ptr_read(audigyls_t * emu, 
					  unsigned int reg, 
					  unsigned int chn)
{
	unsigned long flags;
	unsigned int regptr, val;
  
	regptr = (reg << 16) | chn;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + PTR);
	val = inl(emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return val;
}

static void snd_audigyls_ptr_write(audigyls_t *emu, 
				   unsigned int reg, 
				   unsigned int chn, 
				   unsigned int data)
{
	unsigned int regptr;
	unsigned long flags;

	regptr = (reg << 16) | chn;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + PTR);
	outl(data, emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static void snd_audigyls_intr_enable(audigyls_t *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int enable;
  
	spin_lock_irqsave(&emu->emu_lock, flags);
	enable = inl(emu->port + INTE) | intrenb;
	outl(enable, emu->port + INTE);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static int voice_alloc(audigyls_t *emu, audigyls_voice_t **rvoice)
{
	audigyls_voice_t *voice;
	int idx;

	*rvoice = NULL;
	for (idx = 0; idx < 4; idx ++) {
		voice = &emu->voices[idx];
		if (voice->use)
			continue;
		voice->use = 1;
		*rvoice = voice;
		return 0;
	}
	return -ENOMEM;
}

static int snd_audigyls_voice_alloc(audigyls_t *emu, audigyls_voice_t **rvoice)
{
	unsigned long flags;
	int result;
  
	snd_assert(rvoice != NULL, return -EINVAL);

	spin_lock_irqsave(&emu->voice_lock, flags);
  
	result = voice_alloc(emu, rvoice);

	spin_unlock_irqrestore(&emu->voice_lock, flags);
  
	return result;
}

static int snd_audigyls_voice_free(audigyls_t *emu, audigyls_voice_t *pvoice)
{
	unsigned long flags;
  
	snd_assert(pvoice != NULL, return -EINVAL);
	spin_lock_irqsave(&emu->voice_lock, flags);

	pvoice->interrupt = NULL;
	pvoice->use = 0;
	pvoice->epcm = NULL;

	spin_unlock_irqrestore(&emu->voice_lock, flags);
	return 0;
}

static void snd_audigyls_pcm_free_substream(snd_pcm_runtime_t *runtime)
{
	audigyls_pcm_t *epcm = snd_magic_cast(audigyls_pcm_t, runtime->private_data, return);
  
	if (epcm)
		snd_magic_kfree(epcm);
}

static void snd_audigyls_pcm_interrupt(audigyls_t *emu, audigyls_voice_t *voice)
{
	audigyls_pcm_t *epcm;
  
	if ((epcm = voice->epcm) == NULL)
		return;
	if (epcm->substream == NULL)
		return;
	snd_pcm_period_elapsed(epcm->substream);
}

#if 0  /* not used */
static void snd_audigyls_pcm_channel_interrupt(audigyls_t *emu, audigyls_voice_t *voice)
{
	audigyls_pcm_t *epcm;
	if ((epcm = voice->epcm) == NULL)
		return;
	if (epcm->substream == NULL)
		return;
	snd_pcm_period_elapsed(epcm->substream);
}
#endif


/* open_playback callback */
static int snd_audigyls_pcm_open_playback_channel(snd_pcm_substream_t *substream, int channel_id)
{
	audigyls_t *chip = snd_pcm_substream_chip(substream);
        audigyls_channel_t *channel = &(chip->channels[channel_id]);
	audigyls_pcm_t *epcm;
	snd_pcm_runtime_t *runtime = substream->runtime;

	epcm = snd_magic_kcalloc(audigyls_pcm_t, 0, GFP_KERNEL);
	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = chip;
	epcm->substream = substream;
        epcm->channel_id=channel_id;
  
	runtime->private_data = epcm;
	runtime->private_free = snd_audigyls_pcm_free_substream;
  
	runtime->hw = snd_audigyls_playback_hw;

        channel->emu = chip;
        channel->number = channel_id;

        channel->use=1;
        //printk("open:channel_id=%d, chip=%p, channel=%p\n",channel_id, chip, channel);
        //channel->interrupt = snd_audigyls_pcm_channel_interrupt;
        channel->epcm=epcm;
	return 0;
}

/* close callback */
static int snd_audigyls_pcm_close_playback(snd_pcm_substream_t *substream)
{
	audigyls_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
        audigyls_pcm_t *epcm = snd_magic_cast(audigyls_pcm_t, runtime->private_data, return -ENXIO);
        chip->channels[epcm->channel_id].use=0;
/* FIXME: maybe zero others */
	return 0;
}

static int snd_audigyls_pcm_open_playback_front(snd_pcm_substream_t *substream)
{
	return snd_audigyls_pcm_open_playback_channel(substream, PCM_FRONT_CHANNEL);
}

static int snd_audigyls_pcm_open_playback_center_lfe(snd_pcm_substream_t *substream)
{
	return snd_audigyls_pcm_open_playback_channel(substream, PCM_CENTER_LFE_CHANNEL);
}

static int snd_audigyls_pcm_open_playback_unknown(snd_pcm_substream_t *substream)
{
	return snd_audigyls_pcm_open_playback_channel(substream, PCM_UNKNOWN_CHANNEL);
}

static int snd_audigyls_pcm_open_playback_rear(snd_pcm_substream_t *substream)
{
	return snd_audigyls_pcm_open_playback_channel(substream, PCM_REAR_CHANNEL);
}

/* open_capture callback */
static int snd_audigyls_pcm_open_capture_channel(snd_pcm_substream_t *substream, int channel_id)
{
	audigyls_t *chip = snd_pcm_substream_chip(substream);
        audigyls_channel_t *channel = &(chip->capture_channel);
	audigyls_pcm_t *epcm;
	snd_pcm_runtime_t *runtime = substream->runtime;

	epcm = snd_magic_kcalloc(audigyls_pcm_t, 0, GFP_KERNEL);
	if (epcm == NULL) {
                snd_printk("open_capture_channel: failed epcm alloc\n");
		return -ENOMEM;
        }
	epcm->emu = chip;
	epcm->substream = substream;
        epcm->channel_id=channel_id;
  
	runtime->private_data = epcm;
	runtime->private_free = snd_audigyls_pcm_free_substream;
  
	runtime->hw = snd_audigyls_capture_hw;

        channel->emu = chip;
        channel->number = channel_id;

        channel->use=1;
        //printk("open:channel_id=%d, chip=%p, channel=%p\n",channel_id, chip, channel);
        //channel->interrupt = snd_audigyls_pcm_channel_interrupt;
        channel->epcm=epcm;
	return 0;
}

/* close callback */
static int snd_audigyls_pcm_close_capture(snd_pcm_substream_t *substream)
{
	audigyls_t *chip = snd_pcm_substream_chip(substream);
	//snd_pcm_runtime_t *runtime = substream->runtime;
        //audigyls_pcm_t *epcm = snd_magic_cast(audigyls_pcm_t, runtime->private_data, return);
        chip->capture_channel.use=0;
/* FIXME: maybe zero others */
	return 0;
}

static int snd_audigyls_pcm_open_capture(snd_pcm_substream_t *substream)
{
	return snd_audigyls_pcm_open_capture_channel(substream, 0);
}

/* hw_params callback */
static int snd_audigyls_pcm_hw_params_playback(snd_pcm_substream_t *substream,
				      snd_pcm_hw_params_t * hw_params)
{
	int err;
	snd_pcm_runtime_t *runtime = substream->runtime;
	audigyls_pcm_t *epcm = snd_magic_cast(audigyls_pcm_t, runtime->private_data, return -ENXIO);

	if (! epcm->voice) {
		if ((err = snd_audigyls_voice_alloc(epcm->emu, &epcm->voice)) < 0)
			return err;
		epcm->voice->epcm = epcm;
		epcm->voice->interrupt = snd_audigyls_pcm_interrupt;
	}

	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

/* hw_free callback */
static int snd_audigyls_pcm_hw_free_playback(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	audigyls_pcm_t *epcm;

	if (runtime->private_data == NULL)
		return 0;
	epcm = snd_magic_cast(audigyls_pcm_t, runtime->private_data, return -ENXIO);

	if (epcm->voice) {
		snd_audigyls_voice_free(epcm->emu, epcm->voice);
		epcm->voice = NULL;
	}

	return snd_pcm_lib_free_pages(substream);
}

/* hw_params callback */
static int snd_audigyls_pcm_hw_params_capture(snd_pcm_substream_t *substream,
				      snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

/* hw_free callback */
static int snd_audigyls_pcm_hw_free_capture(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;

	if (runtime->private_data == NULL)
		return 0;

	return snd_pcm_lib_free_pages(substream);
}

/* prepare playback callback */
static int snd_audigyls_pcm_prepare_playback(snd_pcm_substream_t *substream)
{
	audigyls_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	audigyls_pcm_t *epcm = snd_magic_cast(audigyls_pcm_t, runtime->private_data, return -ENXIO);
	int voice = epcm->voice->number;
        voice=epcm->channel_id;
        //printk("prepare:voice_number=%d, rate=%d, format=0x%x, channels=%d, buffer_size=%ld, period_size=%ld, frames_to_bytes=%d\n",voice, runtime->rate, runtime->format, runtime->channels, runtime->buffer_size, runtime->period_size,  frames_to_bytes(runtime, 1));
	snd_audigyls_ptr_write(emu, 0x00, voice, 0);
	snd_audigyls_ptr_write(emu, 0x01, voice, 0);
	snd_audigyls_ptr_write(emu, 0x02, voice, 0);
	snd_audigyls_ptr_write(emu, PLAYBACK_DMA_ADDR, voice, runtime->dma_addr);
	snd_audigyls_ptr_write(emu, PLAYBACK_BUFFER_SIZE, voice, frames_to_bytes(runtime, runtime->buffer_size)<<16); // buffer size in bytes
	snd_audigyls_ptr_write(emu, PLAYBACK_POINTER, voice, 0);
	snd_audigyls_ptr_write(emu, 0x07, voice, 0);
	snd_audigyls_ptr_write(emu, 0x08, voice, 0);
        snd_audigyls_ptr_write(emu, 0x67, 0x0, 0x76543210); /* FIXME: What is this */
#if 0
	snd_audigyls_ptr_write(emu, SPCS0, 0,
			       SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
			       SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
			       SPCS_GENERATIONSTATUS | 0x00001200 |
			       0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT | SPCS_NOTAUDIODATA);
#endif
	snd_audigyls_ptr_write(emu, SPCS0, 0, 0x2108006);
	//snd_audigyls_ptr_write(emu, 0x75, 0, 0x33); /* Routing of some sort */
	//snd_audigyls_ptr_write(emu, 0x75, 0, 0x11 << voice); /* Routing of some sort */
	snd_audigyls_ptr_write(emu, 0x75, 0, snd_audigyls_ptr_read(emu, 0x75, 0) | (0x11<<voice));

	return 0;
}

/* prepare capture callback */
static int snd_audigyls_pcm_prepare_capture(snd_pcm_substream_t *substream)
{
	audigyls_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	audigyls_pcm_t *epcm = snd_magic_cast(audigyls_pcm_t, runtime->private_data, return -ENXIO);
	int voice = epcm->voice->number;
        voice=epcm->channel_id;
        //printk("prepare:voice_number=%d, rate=%d, format=0x%x, channels=%d, buffer_size=%ld, period_size=%ld, frames_to_bytes=%d\n",voice, runtime->rate, runtime->format, runtime->channels, runtime->buffer_size, runtime->period_size,  frames_to_bytes(runtime, 1));
	snd_audigyls_ptr_write(emu, 0x13, voice, 0);
	snd_audigyls_ptr_write(emu, CAPTURE_DMA_ADDR, voice, runtime->dma_addr);
	snd_audigyls_ptr_write(emu, CAPTURE_BUFFER_SIZE, voice, frames_to_bytes(runtime, runtime->buffer_size)<<16); // buffer size in bytes
	snd_audigyls_ptr_write(emu, CAPTURE_POINTER, voice, 0);
        snd_audigyls_ptr_write(emu, 0x60, 0x0, 0x0); /* FIXME: What is this */
	snd_audigyls_ptr_write(emu, 0x75, 0, snd_audigyls_ptr_read(emu, 0x75, 0) | (0x110000<<voice));

	return 0;
}

/* trigger_playback callback */
static int snd_audigyls_pcm_trigger_playback(snd_pcm_substream_t *substream,
				    int cmd)
{
	audigyls_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	audigyls_pcm_t *epcm = snd_magic_cast(audigyls_pcm_t, runtime->private_data, return -ENXIO);
	int channel = epcm->voice->number;
	int result = 0;
        channel=epcm->channel_id;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_audigyls_ptr_write(emu, 0x40, 0, snd_audigyls_ptr_read(emu, 0x40, 0)|(0x1<<channel));
		epcm->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_audigyls_ptr_write(emu, 0x40, 0, snd_audigyls_ptr_read(emu, 0x40, 0) & ~(0x1<<channel));
		snd_audigyls_ptr_write(emu, 0x75, 0, snd_audigyls_ptr_read(emu, 0x75, 0) & ~(0x11<<channel));
		epcm->running = 0;
		break;
	default:
		result = -EINVAL;
		break;
	}
	return result;
}

/* trigger_capture callback */
static int snd_audigyls_pcm_trigger_capture(snd_pcm_substream_t *substream,
				    int cmd)
{
	audigyls_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	audigyls_pcm_t *epcm = snd_magic_cast(audigyls_pcm_t, runtime->private_data, return -ENXIO);
	int channel = epcm->voice->number;
	int result = 0;
        channel=epcm->channel_id;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_audigyls_ptr_write(emu, 0x40, 0, snd_audigyls_ptr_read(emu, 0x40, 0)|(0x100<<channel));
		epcm->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_audigyls_ptr_write(emu, 0x40, 0, snd_audigyls_ptr_read(emu, 0x40, 0) & ~(0x100<<channel));
		snd_audigyls_ptr_write(emu, 0x75, 0, snd_audigyls_ptr_read(emu, 0x75, 0) & ~(0x110000<<channel));
		epcm->running = 0;
		break;
	default:
		result = -EINVAL;
		break;
	}
	return result;
}

/* pointer_playback callback */
static snd_pcm_uframes_t
snd_audigyls_pcm_pointer_playback(snd_pcm_substream_t *substream)
{
	audigyls_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	audigyls_pcm_t *epcm = snd_magic_cast(audigyls_pcm_t, runtime->private_data, return -ENXIO);
	snd_pcm_uframes_t ptr, ptr1, ptr2 = 0;
	int channel = epcm->voice->number;
        channel=epcm->channel_id;

	if (!epcm->running)
		return 0;

	ptr1 = snd_audigyls_ptr_read(emu, PLAYBACK_POINTER, channel);
	ptr2 = bytes_to_frames(runtime, ptr1);
	ptr=ptr2;
        if (ptr >= runtime->buffer_size)
		ptr -= runtime->buffer_size;
	//printk("ptr1 = 0x%lx, ptr2=0x%lx, ptr=0x%lx, buffer_size = 0x%x, period_size = 0x%x, bits=%d, rate=%d\n", ptr1, ptr2, ptr, (int)runtime->buffer_size, (int)runtime->period_size, (int)runtime->frame_bits, (int)runtime->rate);

	return ptr;
}

/* pointer_capture callback */
static snd_pcm_uframes_t
snd_audigyls_pcm_pointer_capture(snd_pcm_substream_t *substream)
{
	audigyls_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	audigyls_pcm_t *epcm = snd_magic_cast(audigyls_pcm_t, runtime->private_data, return -ENXIO);
	snd_pcm_uframes_t ptr, ptr1, ptr2 = 0;
	int channel = epcm->voice->number;
        channel=epcm->channel_id;

	if (!epcm->running)
		return 0;

	ptr1 = snd_audigyls_ptr_read(emu, CAPTURE_POINTER, channel);
	ptr2 = bytes_to_frames(runtime, ptr1);
	ptr=ptr2;
        if (ptr >= runtime->buffer_size)
		ptr -= runtime->buffer_size;
	//printk("ptr1 = 0x%lx, ptr2=0x%lx, ptr=0x%lx, buffer_size = 0x%x, period_size = 0x%x, bits=%d, rate=%d\n", ptr1, ptr2, ptr, (int)runtime->buffer_size, (int)runtime->period_size, (int)runtime->frame_bits, (int)runtime->rate);

	return ptr;
}

/* operators */
static snd_pcm_ops_t snd_audigyls_playback_front_ops = {
	.open =        snd_audigyls_pcm_open_playback_front,
	.close =       snd_audigyls_pcm_close_playback,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_audigyls_pcm_hw_params_playback,
	.hw_free =     snd_audigyls_pcm_hw_free_playback,
	.prepare =     snd_audigyls_pcm_prepare_playback,
	.trigger =     snd_audigyls_pcm_trigger_playback,
	.pointer =     snd_audigyls_pcm_pointer_playback,
};

static snd_pcm_ops_t snd_audigyls_capture_ops = {
	.open =        snd_audigyls_pcm_open_capture,
	.close =       snd_audigyls_pcm_close_capture,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_audigyls_pcm_hw_params_capture,
	.hw_free =     snd_audigyls_pcm_hw_free_capture,
	.prepare =     snd_audigyls_pcm_prepare_capture,
	.trigger =     snd_audigyls_pcm_trigger_capture,
	.pointer =     snd_audigyls_pcm_pointer_capture,
};

static snd_pcm_ops_t snd_audigyls_playback_center_lfe_ops = {
        .open =         snd_audigyls_pcm_open_playback_center_lfe,
        .close =        snd_audigyls_pcm_close_playback,
        .ioctl =        snd_pcm_lib_ioctl,
        .hw_params =    snd_audigyls_pcm_hw_params_playback,
        .hw_free =      snd_audigyls_pcm_hw_free_playback,
        .prepare =      snd_audigyls_pcm_prepare_playback,     
        .trigger =      snd_audigyls_pcm_trigger_playback,  
        .pointer =      snd_audigyls_pcm_pointer_playback, 
};

static snd_pcm_ops_t snd_audigyls_playback_unknown_ops = {
        .open =         snd_audigyls_pcm_open_playback_unknown,
        .close =        snd_audigyls_pcm_close_playback,
        .ioctl =        snd_pcm_lib_ioctl,
        .hw_params =    snd_audigyls_pcm_hw_params_playback,
        .hw_free =      snd_audigyls_pcm_hw_free_playback,
        .prepare =      snd_audigyls_pcm_prepare_playback,     
        .trigger =      snd_audigyls_pcm_trigger_playback,  
        .pointer =      snd_audigyls_pcm_pointer_playback, 
};

static snd_pcm_ops_t snd_audigyls_playback_rear_ops = {
        .open =         snd_audigyls_pcm_open_playback_rear,
        .close =        snd_audigyls_pcm_close_playback,
        .ioctl =        snd_pcm_lib_ioctl,
        .hw_params =    snd_audigyls_pcm_hw_params_playback,
		.hw_free =      snd_audigyls_pcm_hw_free_playback,
        .prepare =      snd_audigyls_pcm_prepare_playback,     
        .trigger =      snd_audigyls_pcm_trigger_playback,  
        .pointer =      snd_audigyls_pcm_pointer_playback, 
};


static unsigned short snd_audigyls_ac97_read(ac97_t *ac97,
					     unsigned short reg)
{
	audigyls_t *emu = snd_magic_cast(audigyls_t, ac97->private_data, return -ENXIO);
	unsigned long flags;
	unsigned short val;
  
	spin_lock_irqsave(&emu->emu_lock, flags);
	outb(reg, emu->port + AC97ADDRESS);
	val = inw(emu->port + AC97DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return val;
}

static void snd_audigyls_ac97_write(ac97_t *ac97,
				    unsigned short reg, unsigned short val)
{
	audigyls_t *emu = snd_magic_cast(audigyls_t, ac97->private_data, return);
	unsigned long flags;
  
	spin_lock_irqsave(&emu->emu_lock, flags);
	outb(reg, emu->port + AC97ADDRESS);
	outw(val, emu->port + AC97DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static int snd_audigyls_ac97(audigyls_t *chip)
{
	ac97_bus_t bus, *pbus;
	ac97_t ac97;
	int err;
  
	memset(&bus, 0, sizeof(bus));
	bus.write = snd_audigyls_ac97_write;
	bus.read = snd_audigyls_ac97_read;
	if ((err = snd_ac97_bus(chip->card, &bus, &pbus)) < 0)
		return err;
	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	return snd_ac97_mixer(pbus, &ac97, &chip->ac97);
}

static int snd_audigyls_free(audigyls_t *chip)
{
	snd_audigyls_ptr_write(chip, 0x40, 0, 0);
	// disable interrupts
	outl(0, chip->port + INTE);
	// disable audio
	outl(HCFG_LOCKSOUNDCACHE, chip->port + HCFG);

	// release the i/o port
	if (chip->res_port) {
		release_resource(chip->res_port);
		kfree_nocheck(chip->res_port);
	}
	// release the irq
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	// release the data
	snd_magic_kfree(chip);
	return 0;
}

static int snd_audigyls_dev_free(snd_device_t *device)
{
	audigyls_t *chip = snd_magic_cast(audigyls_t,
					  device->device_data, return -ENXIO);
	return snd_audigyls_free(chip);
}

static irqreturn_t snd_audigyls_interrupt(int irq, void *dev_id,
					  struct pt_regs *regs)
{
	unsigned int status;

	audigyls_t *chip = snd_magic_cast(audigyls_t, dev_id, return IRQ_NONE);
	int i;
	int mask;
        unsigned int stat76;
	audigyls_channel_t *pchannel;

	spin_lock(&chip->emu_lock);

	status = inl(chip->port + IPR);

	// call updater, unlock before it
	spin_unlock(&chip->emu_lock);
  
	if (! status)
		return IRQ_NONE;

	//printk(KERN_INFO "interrupt status = %08x, chip=%p, channel=%p\n", status,chip, chip->channels);
        stat76 = snd_audigyls_ptr_read(chip, 0x76, 0);
	//mask = IPR_CH_0_LOOP|IPR_CH_0_HALF_LOOP;
        mask = 0x11; /* 0x1 for one half, 0x10 for the other half period. */
	for(i = 0; i < 4; i++) {
		pchannel = &(chip->channels[i]);
		if(stat76 & mask) {
/* FIXME: Select the correct substream for period elapsed */
			if(pchannel->use) {
                          snd_pcm_period_elapsed(pchannel->epcm->substream);
	                //printk(KERN_INFO "interrupt [%d] used\n", i);
                        }
			//if(pvoice->use && pvoice->interrupt)
		//		pvoice->interrupt(chip, pvoice);
		}
	        //printk(KERN_INFO "channel=%p\n",pchannel);
	        //printk(KERN_INFO "interrupt stat76[%d] = %08x, use=%d, channel=%d\n", i, stat76, pchannel->use, pchannel->number);
		mask <<= 1;
	}
	if(stat76 & 0x110000) {
		if(chip->capture_channel.use) {
                  snd_pcm_period_elapsed(chip->capture_channel.epcm->substream);
                }
        }

        snd_audigyls_ptr_write(chip, 0x76, 0, stat76);
	spin_lock(&chip->emu_lock);
	// acknowledge the interrupt if necessary
	outl(status, chip->port+IPR);

	spin_unlock(&chip->emu_lock);

	return IRQ_HANDLED;
}

static void snd_audigyls_pcm_free(snd_pcm_t *pcm)
{
	audigyls_t *emu = snd_magic_cast(audigyls_t, pcm->private_data, return);
	emu->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __devinit snd_audigyls_pcm(audigyls_t *emu, int device, snd_pcm_t **rpcm)
{
	snd_pcm_t *pcm;
	snd_pcm_substream_t *substream;
	int err;
        int capture=0;
  
	if (rpcm)
		*rpcm = NULL;
        if (device == 0) capture=1; 
	if ((err = snd_pcm_new(emu->card, "audigyls", device, 1, capture, &pcm)) < 0)
		return err;
  
	pcm->private_data = emu;
	pcm->private_free = snd_audigyls_pcm_free;

	switch (device) {
	case 0:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_audigyls_playback_front_ops);
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_audigyls_capture_ops);
          break;
	case 1:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_audigyls_playback_center_lfe_ops);
          break;
	case 2:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_audigyls_playback_unknown_ops);
          break;
	case 3:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_audigyls_playback_rear_ops);
          break;
        }

	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strcpy(pcm->name, "AUDIGYLS");
	emu->pcm = pcm;

	for(substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; 
	    substream; 
	    substream = substream->next)
		if ((err = snd_pcm_lib_preallocate_pages(substream, 
							 SNDRV_DMA_TYPE_DEV, 
							 snd_dma_pci_data(emu->pci), 
							 32*1024, 32*1024)) < 0)
			return err;


	for (substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream; 
	      substream; 
	      substream = substream->next)
 	  if ((err = snd_pcm_lib_preallocate_pages(substream, 
	                                           SNDRV_DMA_TYPE_DEV, 
	                                           snd_dma_pci_data(emu->pci), 
	                                           64*1024, 64*1024)) < 0)
	    return err;
  
	if (rpcm)
		*rpcm = pcm;
  
	return 0;
}

static int __devinit snd_audigyls_create(snd_card_t *card,
					 struct pci_dev *pci,
					 audigyls_t **rchip)
{
	audigyls_t *chip;
	int err;
	int ch;
	static snd_device_ops_t ops = {
		.dev_free = snd_audigyls_dev_free,
	};
  
	*rchip = NULL;
  
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	if (pci_set_dma_mask(pci, 0x0fffffff) < 0 ||
	    pci_set_consistent_dma_mask(pci, 0x0fffffff) < 0) {
		printk(KERN_ERR "error to set 28bit mask DMA\n");
		return -ENXIO;
	}
  
	chip = snd_magic_kcalloc(audigyls_t, 0, GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
  
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	spin_lock_init(&chip->emu_lock);
	spin_lock_init(&chip->voice_lock);
  
	chip->port = pci_resource_start(pci, 0);
	if ((chip->res_port = request_region(chip->port, 8,
					     "My Chip")) == NULL) { 
		snd_audigyls_free(chip);
		printk(KERN_ERR "cannot allocate the port\n");
		return -EBUSY;
	}

	if (request_irq(pci->irq, snd_audigyls_interrupt,
			SA_INTERRUPT|SA_SHIRQ, "AUDIGYLS",
			(void *)chip)) {
		snd_audigyls_free(chip);
		printk(KERN_ERR "cannot grab irq\n");
		return -EBUSY;
	}
	chip->irq = pci->irq;
  
	memset(&chip->dma_dev, 0, sizeof(chip->dma_dev));
	chip->dma_dev.type = SNDRV_DMA_TYPE_DEV;
	chip->dma_dev.dev = snd_dma_pci_data(pci);
  
	if(snd_dma_alloc_pages(&chip->dma_dev, 32 * 1024, &chip->buffer) < 0) {
		snd_audigyls_free(chip);
		return -ENOMEM;
	}

	pci_set_master(pci);
	/* read revision & serial */
	pci_read_config_byte(pci, PCI_REVISION_ID, (char *)&chip->revision);
	pci_read_config_dword(pci, PCI_SUBSYSTEM_VENDOR_ID, &chip->serial);
	pci_read_config_word(pci, PCI_SUBSYSTEM_ID, &chip->model);
	printk(KERN_INFO "Model %04x Rev %08x Serial %08x\n", chip->model,
	       chip->revision, chip->serial);

	outl(0, chip->port + INTE);

	for(ch = 0; ch < 4; ch++) {
		chip->voices[ch].emu = chip;
		chip->voices[ch].number = ch;
	}

	/*
	 *  Init to 0x02109204 :
	 *  Clock accuracy    = 0     (1000ppm)
	 *  Sample Rate       = 2     (48kHz)
	 *  Audio Channel     = 1     (Left of 2)
	 *  Source Number     = 0     (Unspecified)
	 *  Generation Status = 1     (Original for Cat Code 12)
	 *  Cat Code          = 12    (Digital Signal Mixer)
	 *  Mode              = 0     (Mode 0)
	 *  Emphasis          = 0     (None)
	 *  CP                = 1     (Copyright unasserted)
	 *  AN                = 0     (Audio data)
	 *  P                 = 0     (Consumer)
	 */
#if 0
	snd_audigyls_ptr_write(chip, SPCS0, 0,
			       SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
			       SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
			       SPCS_GENERATIONSTATUS | 0x00001200 |
			       0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);
	snd_audigyls_ptr_write(chip, SPCS1, 0,
			       SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
			       SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
			       SPCS_GENERATIONSTATUS | 0x00001200 |
			       0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);
	snd_audigyls_ptr_write(chip, SPCS2, 0,
			       SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
			       SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
			       SPCS_GENERATIONSTATUS | 0x00001200 |
			       0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);
#endif
/* Select analogue output */
        //snd_audigyls_ptr_write(chip, 0x41, 0, 0x70f); // ???
        //snd_audigyls_ptr_write(chip, 0x65, 0, 0x1000);

        /* Write 0x8000 to AC97_REC_GAIN to mute it. */
        outb(AC97_REC_GAIN, chip->port + AC97ADDRESS);
        outw(0x8000, chip->port + AC97DATA);

	snd_audigyls_ptr_write(chip, SPCS0, 0, 0x2108006);
	snd_audigyls_ptr_write(chip, 0x42, 0, 0x2108006);
	snd_audigyls_ptr_write(chip, 0x43, 0, 0x2108006);
	snd_audigyls_ptr_write(chip, 0x44, 0, 0x2108006);

	snd_audigyls_ptr_write(chip, 0x72, 0, 0xf0f003f);

	snd_audigyls_ptr_write(chip, 0x71, 0, 0xf0000000);
	//snd_audigyls_ptr_write(chip, 0x61, 0, 0x0);
	//snd_audigyls_ptr_write(chip, 0x62, 0, 0x0);
	snd_audigyls_ptr_write(chip, PLAYBACK_VOLUME, 0, 0x30303030);

	snd_audigyls_ptr_write(chip, 0x45, 0, 0); /* Analogue out */
	//snd_audigyls_ptr_write(chip, 0x45, 0, 0xf00); /* Digital out */

	outl(0, chip->port+0x18);
	snd_audigyls_intr_enable(chip, 0x105);

	outl(HCFG_LOCKSOUNDCACHE|HCFG_AUDIOENABLE, chip->port+HCFG);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL,
				  chip, &ops)) < 0) {
		snd_audigyls_free(chip);
		return err;
	}
	*rchip = chip;
	return 0;
}

static void snd_audigyls_proc_reg_read32(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	audigyls_t *emu = snd_magic_cast(audigyls_t, entry->private_data, return);
	unsigned long value;
	unsigned long flags;
	int i;
	snd_iprintf(buffer, "Registers:\n\n");
	for(i = 0; i < 0x20; i+=4) {
		spin_lock_irqsave(&emu->emu_lock, flags);
		value = inl(emu->port + i);
		spin_unlock_irqrestore(&emu->emu_lock, flags);
		snd_iprintf(buffer, "Register %02X: %08lX\n", i, value);
	}
}

static void snd_audigyls_proc_reg_read16(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	audigyls_t *emu = snd_magic_cast(audigyls_t, entry->private_data, return);
        unsigned int value;
	unsigned long flags;
	int i;
	snd_iprintf(buffer, "Registers:\n\n");
	for(i = 0; i < 0x20; i+=2) {
		spin_lock_irqsave(&emu->emu_lock, flags);
		value = inw(emu->port + i);
		spin_unlock_irqrestore(&emu->emu_lock, flags);
		snd_iprintf(buffer, "Register %02X: %04X\n", i, value);
	}
}

static void snd_audigyls_proc_reg_read8(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	audigyls_t *emu = snd_magic_cast(audigyls_t, entry->private_data, return);
	unsigned int value;
	unsigned long flags;
	int i;
	snd_iprintf(buffer, "Registers:\n\n");
	for(i = 0; i < 0x20; i+=1) {
		spin_lock_irqsave(&emu->emu_lock, flags);
		value = inb(emu->port + i);
		spin_unlock_irqrestore(&emu->emu_lock, flags);
		snd_iprintf(buffer, "Register %02X: %02X\n", i, value);
	}
}

static void snd_audigyls_proc_reg_read1(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	audigyls_t *emu = snd_magic_cast(audigyls_t, entry->private_data, return);
	unsigned long value;
	int i,j;

	snd_iprintf(buffer, "Registers\n");
	for(i = 0; i < 0x40; i++) {
		snd_iprintf(buffer, "%02X: ",i);
		for (j = 0; j < 4; j++) {
                  value = snd_audigyls_ptr_read(emu, i, j);
		  snd_iprintf(buffer, "%08lX ", value);
                }
	        snd_iprintf(buffer, "\n");
	}
}

static void snd_audigyls_proc_reg_read2(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	audigyls_t *emu = snd_magic_cast(audigyls_t, entry->private_data, return);
	unsigned long value;
	int i,j;

	snd_iprintf(buffer, "Registers\n");
	for(i = 0x40; i < 0x80; i++) {
		snd_iprintf(buffer, "%02X: ",i);
		for (j = 0; j < 4; j++) {
                  value = snd_audigyls_ptr_read(emu, i, j);
		  snd_iprintf(buffer, "%08lX ", value);
                }
	        snd_iprintf(buffer, "\n");
	}
}

static int __devinit snd_audigyls_proc_init(audigyls_t * emu)
{
	snd_info_entry_t *entry;
	
	if(! snd_card_proc_new(emu->card, "audigyls_reg32", &entry))
		snd_info_set_text_ops(entry, emu, 1024, snd_audigyls_proc_reg_read32);
	if(! snd_card_proc_new(emu->card, "audigyls_reg16", &entry))
		snd_info_set_text_ops(entry, emu, 1024, snd_audigyls_proc_reg_read16);
	if(! snd_card_proc_new(emu->card, "audigyls_reg8", &entry))
		snd_info_set_text_ops(entry, emu, 1024, snd_audigyls_proc_reg_read8);
	if(! snd_card_proc_new(emu->card, "audigyls_regs1", &entry))
		snd_info_set_text_ops(entry, emu, 1024, snd_audigyls_proc_reg_read1);
	if(! snd_card_proc_new(emu->card, "audigyls_regs2", &entry))
		snd_info_set_text_ops(entry, emu, 1024, snd_audigyls_proc_reg_read2);
	return 0;
}

static int snd_audigyls_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo, int channel_id)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
        uinfo->count = 2;
        uinfo->value.integer.min = 0;
        uinfo->value.integer.max = 255;
        return 0;
}
static int snd_audigyls_volume_info_front(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int channel_id = PCM_FRONT_CHANNEL;
        return snd_audigyls_volume_info(kcontrol, uinfo, channel_id);
}
static int snd_audigyls_volume_info_center_lfe(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int channel_id = PCM_CENTER_LFE_CHANNEL;
        return snd_audigyls_volume_info(kcontrol, uinfo, channel_id);
}
static int snd_audigyls_volume_info_unknown(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int channel_id = PCM_UNKNOWN_CHANNEL;
        return snd_audigyls_volume_info(kcontrol, uinfo, channel_id);
}
static int snd_audigyls_volume_info_rear(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int channel_id = PCM_REAR_CHANNEL;
        return snd_audigyls_volume_info(kcontrol, uinfo, channel_id);
}



static int snd_audigyls_volume_get(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol, int channel_id)
{
        audigyls_t *emu = snd_kcontrol_chip(kcontrol);
        unsigned int value;

        value = snd_audigyls_ptr_read(emu, PLAYBACK_VOLUME, channel_id);
        ucontrol->value.integer.value[0] = 0xff - ((value >> 24) & 0xff); /* Left */
        ucontrol->value.integer.value[1] = 0xff - ((value >> 16) & 0xff); /* Right */
        return 0;
}

static int snd_audigyls_volume_get_front(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = PCM_FRONT_CHANNEL;
        return snd_audigyls_volume_get(kcontrol, ucontrol, channel_id);
}

static int snd_audigyls_volume_get_center_lfe(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = PCM_CENTER_LFE_CHANNEL;
        return snd_audigyls_volume_get(kcontrol, ucontrol, channel_id);
}
static int snd_audigyls_volume_get_unknown(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = PCM_UNKNOWN_CHANNEL;
        return snd_audigyls_volume_get(kcontrol, ucontrol, channel_id);
}
static int snd_audigyls_volume_get_rear(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = PCM_REAR_CHANNEL;
        return snd_audigyls_volume_get(kcontrol, ucontrol, channel_id);
}
                                                                                                                           
static int snd_audigyls_volume_put(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol, int channel_id)
{
        audigyls_t *emu = snd_kcontrol_chip(kcontrol);
        unsigned int value;
        value = snd_audigyls_ptr_read(emu, PLAYBACK_VOLUME, channel_id);
        value = value & 0xffff;
        value = value | ((0xff - ucontrol->value.integer.value[0]) << 24) | ((0xff - ucontrol->value.integer.value[1]) << 16);
        snd_audigyls_ptr_write(emu, PLAYBACK_VOLUME, channel_id, value);
        return 1;
}
static int snd_audigyls_volume_put_front(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = PCM_FRONT_CHANNEL;
        return snd_audigyls_volume_put(kcontrol, ucontrol, channel_id);
}
static int snd_audigyls_volume_put_center_lfe(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = PCM_CENTER_LFE_CHANNEL;
        return snd_audigyls_volume_put(kcontrol, ucontrol, channel_id);
}
static int snd_audigyls_volume_put_unknown(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = PCM_UNKNOWN_CHANNEL;
        return snd_audigyls_volume_put(kcontrol, ucontrol, channel_id);
}
static int snd_audigyls_volume_put_rear(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = PCM_REAR_CHANNEL;
        return snd_audigyls_volume_put(kcontrol, ucontrol, channel_id);
}

static snd_kcontrol_new_t snd_audigyls_volume_control_front =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "Front Volume",
        .info =         snd_audigyls_volume_info_front,
        .get =          snd_audigyls_volume_get_front,
        .put =          snd_audigyls_volume_put_front
};
static snd_kcontrol_new_t snd_audigyls_volume_control_center_lfe =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "Center/LFE Volume",
        .info =         snd_audigyls_volume_info_center_lfe,
        .get =          snd_audigyls_volume_get_center_lfe,
        .put =          snd_audigyls_volume_put_center_lfe
};
static snd_kcontrol_new_t snd_audigyls_volume_control_unknown =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "Unknown Volume",
        .info =         snd_audigyls_volume_info_unknown,
        .get =          snd_audigyls_volume_get_unknown,
        .put =          snd_audigyls_volume_put_unknown
};
static snd_kcontrol_new_t snd_audigyls_volume_control_rear =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "Rear Volume",
        .info =         snd_audigyls_volume_info_rear,
        .get =          snd_audigyls_volume_get_rear,
        .put =          snd_audigyls_volume_put_rear
};

static int __devinit snd_audigyls_mixer(audigyls_t *emu)
{
        int err;
        snd_kcontrol_t *kctl;
        snd_card_t *card = emu->card;
        if ((kctl = emu->ctl_front_volume = snd_ctl_new1(&snd_audigyls_volume_control_front, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = emu->ctl_front_volume = snd_ctl_new1(&snd_audigyls_volume_control_rear, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = emu->ctl_front_volume = snd_ctl_new1(&snd_audigyls_volume_control_center_lfe, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = emu->ctl_front_volume = snd_ctl_new1(&snd_audigyls_volume_control_unknown, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;

        return 0;
}


static int __devinit snd_audigyls_probe(struct pci_dev *pci,
					const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	audigyls_t *chip;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	if ((err = snd_audigyls_create(card, pci, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_audigyls_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_audigyls_pcm(chip, 1, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_audigyls_pcm(chip, 2, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_audigyls_pcm(chip, 3, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_audigyls_ac97(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
#if 1

	if ((err = snd_audigyls_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
#endif

	snd_audigyls_proc_init(chip);

	strcpy(card->driver, "AudigyLS");
	strcpy(card->shortname, "AUDIGYLS");
	sprintf(card->longname, "%s at 0x%lx irq %i",
		card->shortname, chip->port, chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_audigyls_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

// PCI IDs
static struct pci_device_id snd_audigyls_ids[] = {
	{ 0x1102, 0x0007, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* Audigy LS */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, snd_audigyls_ids);

// pci_driver definition
static struct pci_driver driver = {
	.name = "AudigyLS",
	.id_table = snd_audigyls_ids,
	.probe = snd_audigyls_probe,
	.remove = __devexit_p(snd_audigyls_remove),
};

// initialization of the module
static int __init alsa_card_audigyls_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) > 0)
		return err;

	return 0;
}

// clean up the module
static void __exit alsa_card_audigyls_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_audigyls_init)
module_exit(alsa_card_audigyls_exit)
     
EXPORT_NO_SYMBOLS; /* for old kernels only */