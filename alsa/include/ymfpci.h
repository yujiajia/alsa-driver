#ifndef __YMFPCI_H
#define __YMFPCI_H

/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Definitions for Yahama YMF724/740/744/754 chips
 *
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
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "pcm.h"
#include "mixer.h"
#include "rawmidi.h"
#include "ac97_codec.h"

#ifndef PCI_VENDOR_ID_YAMAHA
#define PCI_VENDOR_ID_YAMAHA            0x1073
#endif
#ifndef PCI_DEVICE_ID_YAMAHA_724
#define PCI_DEVICE_ID_YAMAHA_724	0x0004
#endif
#ifndef PCI_DEVICE_ID_YAMAHA_724F
#define PCI_DEVICE_ID_YAMAHA_724F	0x000d
#endif
#ifndef PCI_DEVICE_ID_YAMAHA_740
#define PCI_DEVICE_ID_YAMAHA_740	0x000a
#endif
#ifndef PCI_DEVICE_ID_YAMAHA_740C
#define PCI_DEVICE_ID_YAMAHA_740C	0x000c
#endif
#ifndef PCI_DEVICE_ID_YAMAHA_744
#define PCI_DEVICE_ID_YAMAHA_744	0x0010
#endif
#ifndef PCI_DEVICE_ID_YAMAHA_754
#define PCI_DEVICE_ID_YAMAHA_754	0x0012
#endif

/*
 *  Direct registers
 */

#define YMFREG(codec, reg)		(codec->port + YDSXGR_##reg)

#define	YDSXGR_INTFLAG			0x0004
#define	YDSXGR_ACTIVITY			0x0006
#define	YDSXGR_GLOBALCTRL		0x0008
#define	YDSXGR_ZVCTRL			0x000A
#define	YDSXGR_TIMERCTRL		0x0010
#define	YDSXGR_TIMERCOUNT		0x0012
#define	YDSXGR_SPDIFOUTCTRL		0x0018
#define	YDSXGR_SPDIFOUTSTATUS		0x001C
#define	YDSXGR_EEPROMCTRL		0x0020
#define	YDSXGR_SPDIFINCTRL		0x0034
#define	YDSXGR_SPDIFINSTATUS		0x0038
#define	YDSXGR_DSPPROGRAMDL		0x0048
#define	YDSXGR_DLCNTRL			0x004C
#define	YDSXGR_GPIOININTFLAG		0x0050
#define	YDSXGR_GPIOININTENABLE		0x0052
#define	YDSXGR_GPIOINSTATUS		0x0054
#define	YDSXGR_GPIOOUTCTRL		0x0056
#define	YDSXGR_GPIOFUNCENABLE		0x0058
#define	YDSXGR_GPIOTYPECONFIG		0x005A
#define	YDSXGR_AC97CMDDATA		0x0060
#define	YDSXGR_AC97CMDADR		0x0062
#define	YDSXGR_PRISTATUSDATA		0x0064
#define	YDSXGR_PRISTATUSADR		0x0066
#define	YDSXGR_SECSTATUSDATA		0x0068
#define	YDSXGR_SECSTATUSADR		0x006A
#define	YDSXGR_SECCONFIG		0x0070
#define	YDSXGR_LEGACYOUTVOL		0x0080
#define	YDSXGR_LEGACYOUTVOLL		0x0080
#define	YDSXGR_LEGACYOUTVOLR		0x0082
#define	YDSXGR_NATIVEDACOUTVOL		0x0084
#define	YDSXGR_NATIVEDACOUTVOLL		0x0084
#define	YDSXGR_NATIVEDACOUTVOLR		0x0086
#define	YDSXGR_SPDIFOUTVOL		0x0088
#define	YDSXGR_SPDIFOUTVOLL		0x0088
#define	YDSXGR_SPDIFOUTVOLR		0x008A
#define	YDSXGR_AC3OUTVOL		0x008C
#define	YDSXGR_AC3OUTVOLL		0x008C
#define	YDSXGR_AC3OUTVOLR		0x008E
#define	YDSXGR_PRIADCOUTVOL		0x0090
#define	YDSXGR_PRIADCOUTVOLL		0x0090
#define	YDSXGR_PRIADCOUTVOLR		0x0092
#define	YDSXGR_LEGACYLOOPVOL		0x0094
#define	YDSXGR_LEGACYLOOPVOLL		0x0094
#define	YDSXGR_LEGACYLOOPVOLR		0x0096
#define	YDSXGR_NATIVEDACLOOPVOL		0x0098
#define	YDSXGR_NATIVEDACLOOPVOLL	0x0098
#define	YDSXGR_NATIVEDACLOOPVOLR	0x009A
#define	YDSXGR_SPDIFLOOPVOL		0x009C
#define	YDSXGR_SPDIFLOOPVOLL		0x009E
#define	YDSXGR_SPDIFLOOPVOLR		0x009E
#define	YDSXGR_AC3LOOPVOL		0x00A0
#define	YDSXGR_AC3LOOPVOLL		0x00A0
#define	YDSXGR_AC3LOOPVOLR		0x00A2
#define	YDSXGR_PRIADCLOOPVOL		0x00A4
#define	YDSXGR_PRIADCLOOPVOLL		0x00A4
#define	YDSXGR_PRIADCLOOPVOLR		0x00A6
#define	YDSXGR_NATIVEADCINVOL		0x00A8
#define	YDSXGR_NATIVEADCINVOLL		0x00A8
#define	YDSXGR_NATIVEADCINVOLR		0x00AA
#define	YDSXGR_NATIVEDACINVOL		0x00AC
#define	YDSXGR_NATIVEDACINVOLL		0x00AC
#define	YDSXGR_NATIVEDACINVOLR		0x00AE
#define	YDSXGR_BUF441OUTVOL		0x00B0
#define	YDSXGR_BUF441OUTVOLL		0x00B0
#define	YDSXGR_BUF441OUTVOLR		0x00B2
#define	YDSXGR_BUF441LOOPVOL		0x00B4
#define	YDSXGR_BUF441LOOPVOLL		0x00B4
#define	YDSXGR_BUF441LOOPVOLR		0x00B6
#define	YDSXGR_SPDIFOUTVOL2		0x00B8
#define	YDSXGR_SPDIFOUTVOL2L		0x00B8
#define	YDSXGR_SPDIFOUTVOL2R		0x00BA
#define	YDSXGR_SPDIFLOOPVOL2		0x00BC
#define	YDSXGR_SPDIFLOOPVOL2L		0x00BC
#define	YDSXGR_SPDIFLOOPVOL2R		0x00BE
#define	YDSXGR_ADCSLOTSR		0x00C0
#define	YDSXGR_RECSLOTSR		0x00C4
#define	YDSXGR_ADCFORMAT		0x00C8
#define	YDSXGR_RECFORMAT		0x00CC
#define	YDSXGR_P44SLOTSR		0x00D0
#define	YDSXGR_STATUS			0x0100
#define	YDSXGR_CTRLSELECT		0x0104
#define	YDSXGR_MODE			0x0108
#define	YDSXGR_SAMPLECOUNT		0x010C
#define	YDSXGR_NUMOFSAMPLES		0x0110
#define	YDSXGR_CONFIG			0x0114
#define	YDSXGR_PLAYCTRLSIZE		0x0140
#define	YDSXGR_RECCTRLSIZE		0x0144
#define	YDSXGR_EFFCTRLSIZE		0x0148
#define	YDSXGR_WORKSIZE			0x014C
#define	YDSXGR_MAPOFREC			0x0150
#define	YDSXGR_MAPOFEFFECT		0x0154
#define	YDSXGR_PLAYCTRLBASE		0x0158
#define	YDSXGR_RECCTRLBASE		0x015C
#define	YDSXGR_EFFCTRLBASE		0x0160
#define	YDSXGR_WORKBASE			0x0164
#define	YDSXGR_DSPINSTRAM		0x1000
#define	YDSXGR_CTRLINSTRAM		0x4000

#define YDSXG_AC97READCMD		0x8000
#define YDSXG_AC97WRITECMD		0x0000

#define PCIR_DSXGCTRL			0x48

#define YDSXG_DSPLENGTH			0x0080
#define YDSXG_CTRLLENGTH		0x3000

#define YDSXG_DEFAULT_WORK_SIZE		0x0400

#define YDSXG_PLAYBACK_VOICES		64
#define YDSXG_CAPTURE_VOICES		2
#define YDSXG_EFFECT_VOICES		5

/*
 *
 */

typedef struct snd_stru_ymfpci_playback_bank {
	u32 format;
	u32 loop_default;
	u32 base;			/* 32-bit address */
	u32 loop_start;			/* 32-bit offset */
	u32 loop_end;			/* 32-bit offset */
	u32 loop_frac;			/* 8-bit fraction - loop_start */
	u32 delta_end;			/* pitch delta end */
	u32 lpfK_end;
	u32 eg_gain_end;
	u32 left_gain_end;
	u32 right_gain_end;
	u32 eff1_gain_end;
	u32 eff2_gain_end;
	u32 eff3_gain_end;
	u32 lpfQ;
	u32 status;
	u32 num_of_frames;
	u32 loop_count;
	u32 start;
	u32 start_frac;
	u32 delta;
	u32 lpfK;
	u32 eg_gain;
	u32 left_gain;
	u32 right_gain;
	u32 eff1_gain;
	u32 eff2_gain;
	u32 eff3_gain;
	u32 lpfD1;
	u32 lpfD2;
} snd_ymfpci_playback_bank_t;

typedef struct snd_stru_ymfpci_capture_bank {
	u32 base;			/* 32-bit address */
	u32 loop_end;			/* 32-bit offset */
	u32 start;			/* 32-bit offset */
	u32 num_of_loops;		/* counter */
} snd_ymfpci_capture_bank_t;

typedef struct snd_stru_ymfpci_effect_bank {
	u32 base;			/* 32-bit address */
	u32 loop_end;			/* 32-bit offset */
	u32 start;			/* 32-bit offset */
	u32 temp;
} snd_ymfpci_effect_bank_t;

typedef struct snd_stru_ymfpci_voice ymfpci_voice_t;
typedef struct snd_stru_ymfpci_pcm ymfpci_pcm_t;
typedef struct snd_stru_ymfpci ymfpci_t;

typedef enum {
	YMFPCI_PCM,
	YMFPCI_SYNTH,
	YMFPCI_MIDI
} ymfpci_voice_type_t;

struct snd_stru_ymfpci_voice {
	ymfpci_t *codec;
	int number;
	int use: 1,
	    pcm: 1,
	    synth: 1,
	    midi: 1;
	snd_ymfpci_playback_bank_t *bank;
	void (*interrupt)(ymfpci_t *codec, ymfpci_voice_t *voice);
	ymfpci_pcm_t *ypcm;
};

typedef enum {
	PLAYBACK_VOICE,
	CAPTURE_REC,
	CAPTURE_AC97,
	EFFECT_DRY_LEFT,
	EFFECT_DRY_RIGHT,
	EFFECT_EFF1,
	EFFECT_EFF2,
	EFFECT_EFF3
} snd_ymfpci_pcm_type_t;

struct snd_stru_ymfpci_pcm {
	ymfpci_t *codec;
	snd_ymfpci_pcm_type_t type;
	snd_pcm_substream_t *substream;
	ymfpci_voice_t *voices[2];	/* playback only */
	int running;
	int spdif;	
	u32 frag_size;			/* cached from runtime->frag_size */
	u32 buffer_size;		/* cached from runtime->buffer_size */
	u32 frag_pos;
	u32 last_pos;
	u32 shift_offset;
	u32 capture_bank_number;
};

struct snd_stru_ymfpci {
	snd_dma_t * dma1ptr;	/* DAC1 */
	snd_dma_t * dma2ptr;	/* ADC */
	snd_dma_t * dma3ptr;	/* AC'97 */
	snd_irq_t * irqptr;

	unsigned int device_id;	/* PCI device ID */
	unsigned int rev;	/* PCI revision */
	unsigned long reg_area_phys;
	unsigned long reg_area_virt;

	void *work_ptr;

	unsigned int bank_size_playback;
	unsigned int bank_size_capture;
	unsigned int bank_size_effect;
	unsigned int work_size;

	void *bank_base_playback;
	void *bank_base_capture;
	void *bank_base_effect;
	void *work_base;

	u32 *ctrl_playback;
	snd_ymfpci_playback_bank_t *bank_playback[YDSXG_PLAYBACK_VOICES][2];
	snd_ymfpci_capture_bank_t *bank_capture[YDSXG_CAPTURE_VOICES][2];
	snd_ymfpci_effect_bank_t *bank_effect[YDSXG_EFFECT_VOICES][2];

	int start_count;

	u32 active_bank;
	ymfpci_voice_t voices[64];

	ac97_t *ac97;

	struct pci_dev *pci;
	snd_card_t *card;
	snd_pcm_t *pcm;
	snd_pcm_t *pcm2;
	snd_pcm_t *pcm_spdif;
	snd_pcm_substream_t *capture_substream[YDSXG_CAPTURE_VOICES];
	snd_pcm_substream_t *effect_substream[YDSXG_EFFECT_VOICES];
	snd_kmixer_t *mixer;

	spinlock_t reg_lock;
	spinlock_t voice_lock;
	snd_info_entry_t *proc_entry;
};

int snd_ymfpci_create(snd_card_t * card,
		      struct pci_dev *pci,
		      snd_dma_t * dma1ptr,
		      snd_dma_t * dma2ptr,
		      snd_dma_t * dma3ptr,
		      snd_irq_t * irqptr,
		      ymfpci_t ** rcodec);
void snd_ymfpci_interrupt(ymfpci_t * codec);

int snd_ymfpci_pcm(ymfpci_t * codec, int device, snd_pcm_t **rpcm);
int snd_ymfpci_pcm2(ymfpci_t * codec, int device, snd_pcm_t **rpcm);
int snd_ymfpci_pcm_spdif(ymfpci_t * codec, int device, snd_pcm_t **rpcm);
int snd_ymfpci_mixer(ymfpci_t * codec, int device, snd_pcm_t *pcm, snd_kmixer_t **rmixer);

int snd_ymfpci_voice_alloc(ymfpci_t * codec, ymfpci_voice_type_t type, int pair, ymfpci_voice_t **rvoice);
int snd_ymfpci_voice_free(ymfpci_t * codec, ymfpci_voice_t *pvoice);

#endif				/* __YMFPCI_H */
