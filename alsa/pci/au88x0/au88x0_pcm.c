/*
 * Vortex PCM ALSA driver.
 *
 * Supports ADB and WT DMA. Unfortunately, WT routing is still a
 * mistery. To discover that, we need to disassemble the windoze
 * driver too.
 *
 *
 */

#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "au88x0.h"

#define chip_t vortex_t
#define VORTEX_PCM_TYPE(x) (x->name[40])

/* hardware definition */
static snd_pcm_hardware_t snd_vortex_playback_hw_adb = {
    .info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_RESUME| SNDRV_PCM_INFO_PAUSE |
             SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP_VALID),
    .formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U8 |
             SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW,
    .rates = SNDRV_PCM_RATE_CONTINUOUS,
    .rate_min = 5000,
    .rate_max = 48000,
    .channels_min = 1,
#ifdef CHIP_AU8830
    .channels_max = 4,
#else
	.channels_max = 2,
#endif
    .buffer_bytes_max = 0x10000,
    .period_bytes_min = 0x100,
    .period_bytes_max = 0x1000,
    .periods_min = 1,
    .periods_max = 64,
};
static snd_pcm_hardware_t snd_vortex_playback_hw_spdif = {
    .info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_RESUME| SNDRV_PCM_INFO_PAUSE |
             SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP_VALID),
    .formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U8 |
             SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE | SNDRV_PCM_FMTBIT_MU_LAW | 
             SNDRV_PCM_FMTBIT_A_LAW,
    .rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
    .rate_min = 32000,
    .rate_max = 48000,
    .channels_min = 1,
	.channels_max = 2,
    .buffer_bytes_max = 0x10000,
    .period_bytes_min = 0x100,
    .period_bytes_max = 0x1000,
    .periods_min = 1,
    .periods_max = 64,
};

#ifndef CHIP_AU8810
static snd_pcm_hardware_t snd_vortex_playback_hw_wt = {
    .info = (SNDRV_PCM_INFO_MMAP |
             SNDRV_PCM_INFO_INTERLEAVED |
             SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP_VALID),
    .formats = SNDRV_PCM_FMTBIT_S16_LE,
    .rates = SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_CONTINUOUS, // SNDRV_PCM_RATE_48000,
    .rate_min = 8000,
    .rate_max = 48000,
    .channels_min = 1,
    .channels_max = 2,
    .buffer_bytes_max = 0x10000,
    .period_bytes_min = 0x0400,
    .period_bytes_max = 0x1000,
    .periods_min = 1,
    .periods_max = 64,
};
#endif
/* open callback */
static int snd_vortex_pcm_open(snd_pcm_substream_t *substream) {
	vortex_t *vortex = snd_pcm_substream_chip(substream);
    snd_pcm_runtime_t *runtime = substream->runtime;
	int err;
	
	/* Force equal size periods */
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
    	return err;
	/* Force DMA 32 bit alignment */
	if ((err = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 4)) < 0)
    	return err;
	
    if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT) {
		if (VORTEX_PCM_TYPE(substream->pcm) == VORTEX_PCM_SPDIF) {
			runtime->hw = snd_vortex_playback_hw_spdif;
			switch (vortex->spdif_sr) {
				case 32000:
					runtime->hw.rates = SNDRV_PCM_RATE_32000;
					break;
				case 44100:
					runtime->hw.rates = SNDRV_PCM_RATE_44100;
					break;
				case 48000:
					runtime->hw.rates = SNDRV_PCM_RATE_48000;
					break;
			}
 		} else
			runtime->hw = snd_vortex_playback_hw_adb;
		substream->runtime->private_data = NULL;
    } 
#ifndef CHIP_AU8810
	else {
		runtime->hw = snd_vortex_playback_hw_wt;
		substream->runtime->private_data = NULL;
    }
#endif	
    return 0;
}

/* close callback */
static int snd_vortex_pcm_close(snd_pcm_substream_t *substream) {
    //vortex_t *chip = snd_pcm_substream_chip(substream);
	stream_t *stream = (stream_t*)substream->runtime->private_data;
	
    // the hardware-specific codes will be here
	if (stream != NULL) {
    	stream->substream = NULL;
		stream->nr_ch = 0;
	}
    substream->runtime->private_data = NULL;
    return 0;
}

/* hw_params callback */
static int snd_vortex_pcm_hw_params(snd_pcm_substream_t *substream, snd_pcm_hw_params_t *hw_params) {
	chip_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	stream_t *stream = (stream_t*)(substream->runtime->private_data);
	int err;

	// Alloc buffer memory.
	err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	if (err < 0) {
		printk(KERN_ERR "Vortex: pcm page alloc failed!\n");
		return err;
	}
	/*
	printk(KERN_INFO "Vortex: periods %d, period_bytes %d, channels = %d\n", params_periods(hw_params),
        	params_period_bytes(hw_params), params_channels(hw_params));
	*/
	// Make audio routes and config buffer DMA.
	if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT) {
		int dma, type = VORTEX_PCM_TYPE(substream->pcm);
		/* Dealloc any routes. */
		if (stream != NULL)
			vortex_adb_allocroute(chip, stream->dma, stream->nr_ch, stream->dir, stream->type);
		/* Alloc routes. */
		dma = vortex_adb_allocroute(chip, -1, params_channels(hw_params), substream->stream, type);
		if (dma < 0)
			return dma;
		stream = substream->runtime->private_data = &chip->dma_adb[dma];
		stream->substream = substream;
		/* Setup Buffers. */
		vortex_adbdma_setbuffers(chip, dma, (u32)(runtime->dma_addr), params_period_bytes(hw_params), params_periods(hw_params));
	}
#ifndef CHIP_AU8810
	else {
		if (stream != NULL)
			vortex_wt_allocroute(chip, substream->number, 0);
		vortex_wt_allocroute(chip, substream->number, params_channels(hw_params));
		stream = substream->runtime->private_data = &chip->dma_wt[substream->number];
		stream->substream = substream;
		vortex_wtdma_setbuffers(chip, substream->number, (u32)(runtime->dma_addr), params_period_bytes(hw_params), params_periods(hw_params));
	}
#endif
	return 0;
}

/* hw_free callback */
static int snd_vortex_pcm_hw_free(snd_pcm_substream_t *substream) {
    chip_t *chip = snd_pcm_substream_chip(substream);
	stream_t *stream = (stream_t*)(substream->runtime->private_data);
	
    // Delete audio routes.
	if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT) {
		if (stream != NULL)
        	vortex_adb_allocroute(chip, stream->dma, stream->nr_ch, stream->dir, stream->type);
	}
#ifndef CHIP_AU8810
	else {
		if (stream != NULL)
        	vortex_wt_allocroute(chip, stream->dma, 0);	
	}
#endif
	substream->runtime->private_data = NULL;

    return snd_pcm_lib_free_pages(substream);
}

/* prepare callback */
static int snd_vortex_pcm_prepare(snd_pcm_substream_t *substream) {
    vortex_t *chip = snd_pcm_substream_chip(substream);
    snd_pcm_runtime_t *runtime = substream->runtime;
    stream_t *stream = (stream_t*)substream->runtime->private_data;
	int dma = stream->dma, fmt, dir;

    // set up the hardware with the current configuration.
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = 1;
	else
		dir = 0;
    fmt = vortex_alsafmt_aspfmt(runtime->format);
	if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT) {
        vortex_adbdma_setmode(chip, dma, 1, dir, fmt, 0/*?*/, 0);
        vortex_adbdma_setstartbuffer(chip, dma, 0);
		if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_SPDIF)
        	vortex_adb_setsrc(chip, dma, runtime->rate, dir);
	}
#ifndef CHIP_AU8810
	else {
        vortex_wtdma_setmode(chip, dma, 1, dir, fmt, 0, 0);
        // FIXME: Set rate (i guess using vortex_wt_writereg() somehow).
        vortex_wtdma_setstartbuffer(chip, dma, 0);
    }
#endif
    return 0;
}

/* trigger callback */
static int snd_vortex_pcm_trigger(snd_pcm_substream_t *substream, int cmd) {
    chip_t *chip = snd_pcm_substream_chip(substream);
    stream_t *stream = (stream_t*)substream->runtime->private_data;
	int dma = stream->dma;
	
    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
        // do something to start the PCM engine
        //printk(KERN_INFO "vortex: start %d\n", dma);
		if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT) {
            stream->fifo_enabled = 1;
            vortex_adbdma_startfifo(chip, dma);
        } 
#ifndef CHIP_AU8810		
		else {
            stream->fifo_enabled = 1;
            vortex_wtdma_startfifo(chip, dma);
        }
#endif
    	break;
    case SNDRV_PCM_TRIGGER_STOP:
        // do something to stop the PCM engine
        //printk(KERN_INFO "vortex: stop %d\n", dma)
		if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT) {
            stream->fifo_enabled = 0;
            vortex_adbdma_stopfifo(chip, dma);
        }
#ifndef CHIP_AU8810
        else {
            stream->fifo_enabled = 0;
            vortex_wtdma_stopfifo(chip, dma);
        }
#endif
        break;
    case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
        //printk(KERN_INFO "vortex: pause %d\n", dma);
		if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT)
            vortex_adbdma_pausefifo(chip, dma);
#ifndef CHIP_AU8810
        else
            vortex_wtdma_pausefifo(chip, dma);
#endif
        break;
    case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
        //printk(KERN_INFO "vortex: resume %d\n", dma);
		if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT)
            vortex_adbdma_resumefifo(chip, dma);
#ifndef CHIP_AU8810
        else
            vortex_wtdma_resumefifo(chip, dma);
#endif
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

/* pointer callback */
static snd_pcm_uframes_t snd_vortex_pcm_pointer(snd_pcm_substream_t *substream) {
    vortex_t *chip = snd_pcm_substream_chip(substream);
    stream_t *stream = (stream_t*)substream->runtime->private_data;
	int dma = stream->dma;
    snd_pcm_uframes_t current_ptr = 0;

	spin_lock(&chip->lock);
	if (VORTEX_PCM_TYPE(substream->pcm) != VORTEX_PCM_WT)
        current_ptr = vortex_adbdma_getlinearpos(chip, dma);
#ifndef CHIP_AU8810
    else
        current_ptr = vortex_wtdma_getlinearpos(chip, dma);
#endif
    //printk(KERN_INFO "vortex: pointer = 0x%x\n", current_ptr);
	spin_unlock(&chip->lock);
    return (bytes_to_frames(substream->runtime, current_ptr));
}

/* operators */
static snd_pcm_ops_t snd_vortex_playback_ops = {
    .open = snd_vortex_pcm_open,
    .close = snd_vortex_pcm_close,
    .ioctl = snd_pcm_lib_ioctl,
    .hw_params = snd_vortex_pcm_hw_params,
    .hw_free = snd_vortex_pcm_hw_free,
    .prepare = snd_vortex_pcm_prepare,
    .trigger = snd_vortex_pcm_trigger,
    .pointer = snd_vortex_pcm_pointer,
    //.page = snd_pcm_sgbuf_ops_page,
};

/*
*  definitions of capture are omitted here...
*/

static char *vortex_pcm_prettyname[VORTEX_PCM_LAST] = {
	"AU88x0 ADB",
	"AU88x0 SPDIF",
	"AU88x0 I2S",
	"AU88x0 A3D",
	"AU88x0 WT",
};
static char *vortex_pcm_name[VORTEX_PCM_LAST] = {
	"adb",
	"spdif",
	"i2s",
	"a3d",
	"wt",
};

/* SPDIF kcontrol */
static int snd_vortex_spdif_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo) {
	static char *texts[] = { "32000", "44100", "48000"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
static int snd_vortex_spdif_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol) {
	vortex_t *vortex = snd_kcontrol_chip(kcontrol);
	
	if (vortex->spdif_sr == 32000)
		ucontrol->value.enumerated.item[0]=0;
	if (vortex->spdif_sr == 44100)
		ucontrol->value.enumerated.item[0]=1;
	if (vortex->spdif_sr == 48000)
		ucontrol->value.enumerated.item[0]=2;
	return 0;
}
static int snd_vortex_spdif_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol) {
	vortex_t * vortex = snd_kcontrol_chip(kcontrol);
	static unsigned int sr[3] = {32000, 44100, 48000};
	
	//printk("vortex: spdif sr = %d\n", ucontrol->value.enumerated.item[0]);
	vortex->spdif_sr = sr[ucontrol->value.enumerated.item[0] % 3];
	vortex_spdif_init(vortex, sr[ucontrol->value.enumerated.item[0] % 3], 1);
	return 1;
}
static snd_kcontrol_new_t vortex_spdif_kcontrol __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "SPDIF SR",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.private_value = 0,
	.info = snd_vortex_spdif_info,
	.get = snd_vortex_spdif_get,
	.put = snd_vortex_spdif_put
};

/* create a pcm device */
int __devinit snd_vortex_new_pcm(vortex_t *chip, int idx, int nr) {
    snd_pcm_t *pcm;
    int err, nr_capt;
	//static int __devinit pcm_idx=0;
    if ((chip == 0)||(idx < 0)||(idx>VORTEX_PCM_LAST))
        return -ENODEV;

	/* idx indicates which kind of PCM device. ADB, SPDIF, I2S and A3D share the 
	 * same dma engine. WT uses it own separate dma engine whcih cant capture. */
	if (idx == VORTEX_PCM_WT)
		nr_capt = 0;
	else
		nr_capt = nr;
	if ((err = snd_pcm_new(chip->card, vortex_pcm_prettyname[idx], idx, nr, nr_capt, &pcm)) < 0)
    	return err;
	strcpy(pcm->name, vortex_pcm_name[idx]);
	chip->pcm[idx] = pcm;
	// This is an evil hack, but it saves a lot of duplicated code.
	VORTEX_PCM_TYPE(pcm) = idx;
    pcm->private_data = chip;
    /* set operators */
    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_vortex_playback_ops);
    if (idx == VORTEX_PCM_ADB)
    	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_vortex_playback_ops);
    /* pre-allocation of buffers */
    snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci_dev, pcm, 0x10000, 0x10000);

	if (VORTEX_PCM_TYPE(pcm) == VORTEX_PCM_SPDIF) {
		snd_kcontrol_t *kcontrol;
		
		if ((kcontrol = snd_ctl_new1(&vortex_spdif_kcontrol, chip)) == NULL)
			return -ENOMEM;
		if ((err = snd_ctl_add(chip->card, kcontrol)) < 0)
			return err;
	}
    return 0;
}
