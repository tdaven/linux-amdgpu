/*
 * AMD ALSA SoC PCM Driver
 *
 * Copyright 2014-2015 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/mfd/amd_acp.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "acp.h"

#define PLAYBACK_MIN_NUM_PERIODS    2
#define PLAYBACK_MAX_NUM_PERIODS    2
#define PLAYBACK_MAX_PERIOD_SIZE    16384
#define PLAYBACK_MIN_PERIOD_SIZE    1024
#define CAPTURE_MIN_NUM_PERIODS     2
#define CAPTURE_MAX_NUM_PERIODS     2
#define CAPTURE_MAX_PERIOD_SIZE     16384
#define CAPTURE_MIN_PERIOD_SIZE     1024

#define NUM_DSCRS_PER_CHANNEL 2

#define MAX_BUFFER (PLAYBACK_MAX_PERIOD_SIZE * PLAYBACK_MAX_NUM_PERIODS)
#define MIN_BUFFER MAX_BUFFER

static const struct snd_pcm_hardware acp_pcm_hardware_playback = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 1,
	.channels_max = 8,
	.rates = SNDRV_PCM_RATE_8000_96000,
	.rate_min = 8000,
	.rate_max = 96000,
	.buffer_bytes_max = PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE,
	.period_bytes_min = PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max = PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min = PLAYBACK_MIN_NUM_PERIODS,
	.periods_max = PLAYBACK_MAX_NUM_PERIODS,
};

static const struct snd_pcm_hardware acp_pcm_hardware_capture = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BATCH |
	    SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 1,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_8000_48000,
	.rate_min = 8000,
	.rate_max = 48000,
	.buffer_bytes_max = CAPTURE_MAX_NUM_PERIODS * CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min = CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max = CAPTURE_MAX_PERIOD_SIZE,
	.periods_min = CAPTURE_MIN_NUM_PERIODS,
	.periods_max = CAPTURE_MAX_NUM_PERIODS,
};

struct audio_drv_data {
	struct snd_pcm_substream *play_stream;
	struct snd_pcm_substream *capture_stream;
	struct acp_irq_prv *iprv;
	void __iomem *acp_mmio;
};

/* ACP DMA irq handler routine for playback, capture usecases */
static irqreturn_t dma_irq_handler(int irq, void *arg)
{
	u16 dscr_idx;
	u32 intr_flag;

	int priority_level = 0;
	struct device *dev = arg;

	struct audio_drv_data *irq_data;
	void __iomem *acp_mmio;

	BUG_ON(dev == NULL);

	irq_data = dev_get_drvdata(dev);
	acp_mmio = irq_data->acp_mmio;

	intr_flag = acp_get_intr_flag(acp_mmio);

	if ((intr_flag & BIT(ACP_TO_I2S_DMA_CH_NUM)) != 0) {
		dscr_idx = get_dscr_idx(acp_mmio, SNDRV_PCM_STREAM_PLAYBACK);
		config_acp_dma_channel(acp_mmio, SYSRAM_TO_ACP_CH_NUM, dscr_idx,
				       1, priority_level);
		acp_dma_start(acp_mmio, SYSRAM_TO_ACP_CH_NUM, false);

		snd_pcm_period_elapsed(irq_data->play_stream);
		acp_ext_stat_clear_dmaioc(acp_mmio, ACP_TO_I2S_DMA_CH_NUM);
	}

	if ((intr_flag & BIT(I2S_TO_ACP_DMA_CH_NUM)) != 0) {
		dscr_idx = get_dscr_idx(acp_mmio, SNDRV_PCM_STREAM_CAPTURE);
		config_acp_dma_channel(acp_mmio, ACP_TO_SYSRAM_CH_NUM, dscr_idx,
				       1, priority_level);
		acp_dma_start(acp_mmio, ACP_TO_SYSRAM_CH_NUM, false);
		acp_ext_stat_clear_dmaioc(acp_mmio, I2S_TO_ACP_DMA_CH_NUM);
	}

	if ((intr_flag & BIT(ACP_TO_SYSRAM_CH_NUM)) != 0) {
		snd_pcm_period_elapsed(irq_data->capture_stream);
		acp_ext_stat_clear_dmaioc(acp_mmio, ACP_TO_SYSRAM_CH_NUM);
	}

	return IRQ_HANDLED;
}

static int acp_dma_open(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *prtd = substream->private_data;
	struct audio_drv_data *intr_data = dev_get_drvdata(prtd->platform->dev);

	struct audio_substream_data *adata =
		kzalloc(sizeof(struct audio_substream_data), GFP_KERNEL);
	if (adata == NULL)
		return -ENOMEM;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		runtime->hw = acp_pcm_hardware_playback;
	else
		runtime->hw = acp_pcm_hardware_capture;

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(prtd->platform->dev, "set integer constraint failed\n");
		return ret;
	}

	adata->acp_mmio = intr_data->acp_mmio;
	runtime->private_data = adata;

	/* Enable ACP irq, when neither playback or capture streams are
	 * active by the time when a new stream is being opened.
	 * This enablement is not required for another stream, if current
	 * stream is not closed */
	if (!intr_data->play_stream && !intr_data->capture_stream)
		acp_enable_external_interrupts(adata->acp_mmio, 1);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		intr_data->play_stream = substream;
	else
		intr_data->capture_stream = substream;

	return 0;
}

static int acp_dma_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	int status;
	uint64_t size;
	struct snd_dma_buffer *dma_buffer;
	struct page *pg;
	u16 num_of_pages;
	struct snd_pcm_runtime *runtime;
	struct audio_substream_data *rtd;

	dma_buffer = &substream->dma_buffer;

	runtime = substream->runtime;
	rtd = runtime->private_data;

	if (WARN_ON(!rtd))
		return -EINVAL;

	size = params_buffer_bytes(params);
	status = snd_pcm_lib_malloc_pages(substream, size);
	if (status < 0)
		return status;

	memset(substream->runtime->dma_area, 0, params_buffer_bytes(params));
	pg = virt_to_page(substream->dma_buffer.area);

	if (pg != NULL) {
		/* Save for runtime private data */
		rtd->pg = pg;
		rtd->order = get_order(size);

		/* Let ACP know the Allocated memory */
		num_of_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;

		/* Fill the page table entries in ACP SRAM */
		rtd->pg = pg;
		rtd->size = size;
		rtd->num_of_pages = num_of_pages;
		rtd->direction = substream->stream;

		config_acp_dma(rtd->acp_mmio, rtd);
		status = 0;
	} else {
		status = -ENOMEM;
	}
	return status;
}

static int acp_dma_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static snd_pcm_uframes_t acp_dma_pointer(struct snd_pcm_substream *substream)
{
	u32 pos = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;

	pos = acp_update_dma_pointer(rtd->acp_mmio, substream->stream,
				frames_to_bytes(runtime, runtime->period_size));
	return bytes_to_frames(runtime, pos);

}

static int acp_dma_mmap(struct snd_pcm_substream *substream,
			struct vm_area_struct *vma)
{
	return snd_pcm_lib_default_mmap(substream, vma);
}

static int acp_dma_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		config_acp_dma_channel(rtd->acp_mmio, SYSRAM_TO_ACP_CH_NUM,
					PLAYBACK_START_DMA_DESCR_CH12,
					NUM_DSCRS_PER_CHANNEL, 0);
		config_acp_dma_channel(rtd->acp_mmio, ACP_TO_I2S_DMA_CH_NUM,
					PLAYBACK_START_DMA_DESCR_CH13,
					NUM_DSCRS_PER_CHANNEL, 0);
		/* Fill ACP SRAM (2 periods) with zeros from System RAM
		 * which is zero-ed in hw_params */
		acp_dma_start(rtd->acp_mmio, SYSRAM_TO_ACP_CH_NUM, false);

		/* ACP SRAM (2 periods of buffer size) is intially filled with
		 * zeros. Before rendering starts, 2nd half of SRAM will be
		 * filled with valid audio data DMA'ed from first half of system
		 * RAM and 1st half of SRAM will be filled with Zeros. This is
		 * the initial scenario when redering starts from SRAM. Later
		 * on, 2nd half of system memory will be DMA'ed to 1st half of
		 * SRAM, 1st half of system memory will be DMA'ed to 2nd half of
		 * SRAM in ping-pong way till rendering stops. */
		config_acp_dma_channel(rtd->acp_mmio, SYSRAM_TO_ACP_CH_NUM,
					PLAYBACK_START_DMA_DESCR_CH12,
					1, 0);
	} else {
		config_acp_dma_channel(rtd->acp_mmio, ACP_TO_SYSRAM_CH_NUM,
					CAPTURE_START_DMA_DESCR_CH14,
					NUM_DSCRS_PER_CHANNEL, 0);
		config_acp_dma_channel(rtd->acp_mmio, I2S_TO_ACP_DMA_CH_NUM,
					CAPTURE_START_DMA_DESCR_CH15,
					NUM_DSCRS_PER_CHANNEL, 0);
	}
	return 0;
}

static int acp_dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret;

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;

	if (!rtd)
		return -EINVAL;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			acp_dma_start(rtd->acp_mmio,
						SYSRAM_TO_ACP_CH_NUM, false);
			prebuffer_audio(rtd->acp_mmio);
			acp_dma_start(rtd->acp_mmio,
					ACP_TO_I2S_DMA_CH_NUM, true);

		} else {
			acp_dma_start(rtd->acp_mmio,
					    I2S_TO_ACP_DMA_CH_NUM, true);

		}

		ret = 0;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			acp_dma_stop(rtd->acp_mmio, SYSRAM_TO_ACP_CH_NUM);
			acp_dma_stop(rtd->acp_mmio, ACP_TO_I2S_DMA_CH_NUM);
		} else {
			acp_dma_stop(rtd->acp_mmio, I2S_TO_ACP_DMA_CH_NUM);
			acp_dma_stop(rtd->acp_mmio, ACP_TO_SYSRAM_CH_NUM);
		}
		ret = 0;
		break;
	default:
		ret = -EINVAL;

	}
	return ret;
}

static int acp_dma_new(struct snd_soc_pcm_runtime *rtd)
{
	return snd_pcm_lib_preallocate_pages_for_all(rtd->pcm,
							SNDRV_DMA_TYPE_DEV,
							NULL, MIN_BUFFER,
							MAX_BUFFER);
}

static int acp_dma_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;
	struct snd_soc_pcm_runtime *prtd = substream->private_data;
	struct audio_drv_data *adata = dev_get_drvdata(prtd->platform->dev);

	kfree(rtd);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		adata->play_stream = NULL;
	else
		adata->capture_stream = NULL;

	/* Disable ACP irq, when the current stream is being closed and
	 * another stream is also not active. */
	if (!adata->play_stream && !adata->capture_stream)
		acp_enable_external_interrupts(adata->acp_mmio, 0);

	pm_runtime_mark_last_busy(prtd->platform->dev);
	return 0;
}

static struct snd_pcm_ops acp_dma_ops = {
	.open = acp_dma_open,
	.close = acp_dma_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = acp_dma_hw_params,
	.hw_free = acp_dma_hw_free,
	.trigger = acp_dma_trigger,
	.pointer = acp_dma_pointer,
	.mmap = acp_dma_mmap,
	.prepare = acp_dma_prepare,
};

static struct snd_soc_platform_driver acp_asoc_platform = {
	.ops = &acp_dma_ops,
	.pcm_new = acp_dma_new,
};

static int acp_audio_probe(struct platform_device *pdev)
{
	int status;
	struct audio_drv_data *audio_drv_data;
	struct resource *res;

	audio_drv_data = devm_kzalloc(&pdev->dev, sizeof(struct audio_drv_data),
					GFP_KERNEL);
	if (audio_drv_data == NULL)
		return -ENOMEM;

	audio_drv_data->iprv = devm_kzalloc(&pdev->dev,
						sizeof(struct acp_irq_prv),
						GFP_KERNEL);
	if (audio_drv_data->iprv == NULL)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	audio_drv_data->acp_mmio = devm_ioremap_resource(&pdev->dev, res);

	/* The following members gets populated in device 'open'
	 * function. Till then interrupts are disabled in 'acp_hw_init'
	 * and device doesn't generate any interrupts.
	 */

	audio_drv_data->play_stream = NULL;
	audio_drv_data->capture_stream = NULL;

	audio_drv_data->iprv->dev = &pdev->dev;
	audio_drv_data->iprv->acp_mmio = audio_drv_data->acp_mmio;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_IRQ FAILED\n");
		return -ENODEV;
	}

	status = devm_request_irq(&pdev->dev, res->start, dma_irq_handler,
					0, "ACP_IRQ", &pdev->dev);
	if (status) {
		dev_err(&pdev->dev, "ACP IRQ request failed\n");
		return status;
	}

	dev_set_drvdata(&pdev->dev, audio_drv_data);

	/* Initialize the ACP */
	acp_hw_init(audio_drv_data->acp_mmio);

	status = snd_soc_register_platform(&pdev->dev, &acp_asoc_platform);
	if (0 != status) {
		dev_err(&pdev->dev, "Fail to register ALSA platform device\n");
		return status;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, 10000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return status;
}

static int acp_audio_remove(struct platform_device *pdev)
{
	struct audio_drv_data *adata = dev_get_drvdata(&pdev->dev);

	acp_hw_deinit(adata->acp_mmio);
	snd_soc_unregister_platform(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int acp_pcm_suspend(struct device *dev)
{
	bool pm_rts;
	struct audio_drv_data *adata = dev_get_drvdata(dev);

	pm_rts = pm_runtime_status_suspended(dev);
	if (pm_rts == false)
		acp_suspend(adata->acp_mmio);

	return 0;
}

static int acp_pcm_resume(struct device *dev)
{
	bool pm_rts;
	struct snd_pcm_substream *stream;
	struct snd_pcm_runtime *rtd;
	struct audio_substream_data *sdata;
	struct audio_drv_data *adata = dev_get_drvdata(dev);

	pm_rts = pm_runtime_status_suspended(dev);
	if (pm_rts == true) {
		/* Resumed from system wide suspend and there is
		 * no pending audio activity to resume. */
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);

		goto out;
	}

	acp_resume(adata->acp_mmio);

	stream = adata->play_stream;
	rtd = stream ? stream->runtime : NULL;
	if (rtd != NULL) {
		/* Resume playback stream from a suspended state */
		sdata = rtd->private_data;
		config_acp_dma(adata->acp_mmio, sdata);
	}

	stream = adata->capture_stream;
	rtd =  stream ? stream->runtime : NULL;
	if (rtd != NULL) {
		/* Resume capture stream from a suspended state */
		sdata = rtd->private_data;
		config_acp_dma(adata->acp_mmio, sdata);
	}
out:
	return 0;
}

static int acp_pcm_runtime_suspend(struct device *dev)
{
	struct audio_drv_data *adata = dev_get_drvdata(dev);

	acp_suspend(adata->acp_mmio);
	return 0;
}

static int acp_pcm_runtime_resume(struct device *dev)
{
	struct audio_drv_data *adata = dev_get_drvdata(dev);

	acp_resume(adata->acp_mmio);
	return 0;
}

static const struct dev_pm_ops acp_pm_ops = {
	.suspend = acp_pcm_suspend,
	.resume = acp_pcm_resume,
	.runtime_suspend = acp_pcm_runtime_suspend,
	.runtime_resume = acp_pcm_runtime_resume,
};

static struct platform_driver acp_dma_driver = {
	.probe = acp_audio_probe,
	.remove = acp_audio_remove,
	.driver = {
		.name = "acp_audio_dma",
		.pm = &acp_pm_ops,
	},
};

module_platform_driver(acp_dma_driver);

MODULE_AUTHOR("Maruthi.Bayyavarapu@amd.com");
MODULE_DESCRIPTION("AMD ACP PCM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:acp-dma-audio");
