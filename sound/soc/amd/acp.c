/*
 * AMD ACP module
 *
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <sound/asound.h>
#include "acp.h"

#include "include/acp_2_2_d.h"
#include "include/acp_2_2_sh_mask.h"

#define VISLANDS30_IV_SRCID_ACP 0x000000a2

static u32 acp_reg_read(void __iomem *acp_mmio, u32 reg)
{
	return readl(acp_mmio + (reg * 4));
}

static void acp_reg_write(u32 val, void __iomem *acp_mmio, u32 reg)
{
	writel(val, acp_mmio + (reg * 4));
}

/* Configure a given dma channel parameters - enable/disble,
 * number of descriptors, priority */
void config_acp_dma_channel(void __iomem *acp_mmio, u8 ch_num,
				   u16 dscr_strt_idx, u16 num_dscrs,
				   enum acp_dma_priority_level priority_level)
{
	u32 dma_ctrl;

	/* disable the channel run field */
	dma_ctrl = acp_reg_read(acp_mmio, mmACP_DMA_CNTL_0 + ch_num);
	dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChRun_MASK;
	acp_reg_write(dma_ctrl, acp_mmio, mmACP_DMA_CNTL_0 + ch_num);

	/* program a DMA channel with first descriptor to be processed. */
	acp_reg_write((ACP_DMA_DSCR_STRT_IDX_0__DMAChDscrStrtIdx_MASK
			& dscr_strt_idx),
			acp_mmio, mmACP_DMA_DSCR_STRT_IDX_0 + ch_num);

	/* program a DMA channel with the number of descriptors to be
	 * processed in the transfer */
	acp_reg_write(ACP_DMA_DSCR_CNT_0__DMAChDscrCnt_MASK & num_dscrs,
		acp_mmio, mmACP_DMA_DSCR_CNT_0 + ch_num);

	/* set DMA channel priority */
	acp_reg_write(priority_level, acp_mmio, mmACP_DMA_PRIO_0 + ch_num);
}

/* Initialize the dma descriptors location in SRAM and page size */
static void acp_dma_descr_init(void __iomem *acp_mmio)
{
	u32 sram_pte_offset = 0;

	/* SRAM starts at 0x04000000. From that offset one page (4KB) left for
	 * filling DMA descriptors.sram_pte_offset = 0x04001000 , used for
	 * filling system RAM's physical pages.
	 * This becomes the ALSA's Ring buffer start address
	 */
	sram_pte_offset = ACP_DAGB_GRP_SRAM_BASE_ADDRESS;

	/* snoopable */
	sram_pte_offset |= ACP_DAGB_BASE_ADDR_GRP_1__AXI2DAGBSnoopSel_MASK;
	/* Memmory is system mmemory */
	sram_pte_offset |= ACP_DAGB_BASE_ADDR_GRP_1__AXI2DAGBTargetMemSel_MASK;
	/* Page Enabled */
	sram_pte_offset |= ACP_DAGB_BASE_ADDR_GRP_1__AXI2DAGBGrpEnable_MASK;

	acp_reg_write(sram_pte_offset,	acp_mmio, mmACP_DAGB_BASE_ADDR_GRP_1);
	acp_reg_write(PAGE_SIZE_4K_ENABLE, acp_mmio,
						mmACP_DAGB_PAGE_SIZE_GRP_1);
}

/* Initialize a dma descriptor in SRAM based on descritor information passed */
static void config_dma_descriptor_in_sram(void __iomem *acp_mmio,
					  u16 descr_idx,
					  acp_dma_dscr_transfer_t *descr_info)
{
	u32 sram_offset;

	sram_offset = (descr_idx * sizeof(acp_dma_dscr_transfer_t));

	/* program the source base address. */
	acp_reg_write(sram_offset, acp_mmio, mmACP_SRBM_Targ_Idx_Addr);
	acp_reg_write(descr_info->src,	acp_mmio, mmACP_SRBM_Targ_Idx_Data);
	/* program the destination base address. */
	acp_reg_write(sram_offset + 4,	acp_mmio, mmACP_SRBM_Targ_Idx_Addr);
	acp_reg_write(descr_info->dest, acp_mmio, mmACP_SRBM_Targ_Idx_Data);

	/* program the number of bytes to be transferred for this descriptor. */
	acp_reg_write(sram_offset + 8,	acp_mmio, mmACP_SRBM_Targ_Idx_Addr);
	acp_reg_write(descr_info->xfer_val, acp_mmio, mmACP_SRBM_Targ_Idx_Data);
}

/* Initialize the DMA descriptor information for transfer between
 * system memory <-> ACP SRAM
 */
static void set_acp_sysmem_dma_descriptors(void __iomem *acp_mmio,
					   u32 size, int direction,
					   u32 pte_offset)
{
	u16 num_descr;
	u16 dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH12;
	acp_dma_dscr_transfer_t dmadscr[2];

	num_descr = 2;

	dmadscr[0].xfer_val = 0;
	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH12;
		dmadscr[0].dest = ACP_SHARED_RAM_BANK_1_ADDRESS + (size / 2);
		dmadscr[0].src = ACP_INTERNAL_APERTURE_WINDOW_0_ADDRESS +
			(pte_offset * PAGE_SIZE_4K);
		dmadscr[0].xfer_val |= (DISABLE << 22) |
			(ACP_DMA_ATTRIBUTES_DAGB_ONION_TO_SHAREDMEM << 16) |
			(size / 2);
	} else {
		dma_dscr_idx = CAPTURE_START_DMA_DESCR_CH14;
		dmadscr[0].src = ACP_SHARED_RAM_BANK_5_ADDRESS;
		dmadscr[0].dest = ACP_INTERNAL_APERTURE_WINDOW_0_ADDRESS +
			(pte_offset * PAGE_SIZE_4K);
		dmadscr[0].xfer_val |=
			(ENABLE << 22) |
			(ACP_DMA_ATTRIBUTES_SHAREDMEM_TO_DAGB_ONION << 16) |
			(size / 2);
	}

	config_dma_descriptor_in_sram(acp_mmio, dma_dscr_idx, &dmadscr[0]);

	dmadscr[1].xfer_val = 0;
	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		dma_dscr_idx = PLAYBACK_END_DMA_DESCR_CH12;
		dmadscr[1].dest = ACP_SHARED_RAM_BANK_1_ADDRESS;
		dmadscr[1].src = ACP_INTERNAL_APERTURE_WINDOW_0_ADDRESS +
			(pte_offset * PAGE_SIZE_4K) + (size / 2);
		dmadscr[1].xfer_val |= (DISABLE << 22) |
			(ACP_DMA_ATTRIBUTES_DAGB_ONION_TO_SHAREDMEM << 16) |
			(size / 2);
	} else {
		dma_dscr_idx = CAPTURE_END_DMA_DESCR_CH14;
		dmadscr[1].dest = dmadscr[0].dest + (size / 2);
		dmadscr[1].src = dmadscr[0].src + (size / 2);
		dmadscr[1].xfer_val |= (ENABLE << 22) |
			(ACP_DMA_ATTRIBUTES_SHAREDMEM_TO_DAGB_ONION << 16) |
			(size / 2);
	}

	config_dma_descriptor_in_sram(acp_mmio, dma_dscr_idx, &dmadscr[1]);

	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		/* starting descriptor for this channel */
		dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH12;
		config_acp_dma_channel(acp_mmio, SYSRAM_TO_ACP_CH_NUM,
					dma_dscr_idx, num_descr,
					ACP_DMA_PRIORITY_LEVEL_NORMAL);
	} else {
		/* starting descriptor for this channel */
		dma_dscr_idx = CAPTURE_START_DMA_DESCR_CH14;
		config_acp_dma_channel(acp_mmio, ACP_TO_SYSRAM_CH_NUM,
					dma_dscr_idx, num_descr,
					ACP_DMA_PRIORITY_LEVEL_NORMAL);
	}
}

/* Initialize the DMA descriptor information for transfer between
 * ACP SRAM <-> I2S
 */
static void set_acp_to_i2s_dma_descriptors(void __iomem *acp_mmio,
					   u32 size, int direction)
{

	u16 num_descr;
	u16 dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH13;
	acp_dma_dscr_transfer_t dmadscr[2];

	num_descr = 2;

	dmadscr[0].xfer_val = 0;
	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH13;
		dmadscr[0].src = ACP_SHARED_RAM_BANK_1_ADDRESS;
		/* dmadscr[0].dest is unused by hardware. Assgned to 0 to
		 * remove compiler warning */
		dmadscr[0].dest = 0;
		dmadscr[0].xfer_val |= (ENABLE << 22) | (TO_ACP_I2S_1 << 16) |
					(size / 2);
	} else {
		dma_dscr_idx = CAPTURE_START_DMA_DESCR_CH15;
		/* dmadscr[0].src is unused by hardware. Assgned to 0 to
		 * remove compiler warning */
		dmadscr[0].src = 0;
		dmadscr[0].dest = ACP_SHARED_RAM_BANK_5_ADDRESS;
		dmadscr[0].xfer_val |= (ENABLE << 22) |
					(FROM_ACP_I2S_1 << 16) | (size / 2);
	}

	config_dma_descriptor_in_sram(acp_mmio, dma_dscr_idx, &dmadscr[0]);

	dmadscr[1].xfer_val = 0;
	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		dma_dscr_idx = PLAYBACK_END_DMA_DESCR_CH13;
		dmadscr[1].src = dmadscr[0].src + (size / 2);
		/* dmadscr[1].dest is unused by hardware. Assgned to 0 to
		 * remove compiler warning */
		dmadscr[1].dest = 0;
		dmadscr[1].xfer_val |= (ENABLE << 22) | (TO_ACP_I2S_1 << 16) |
					(size / 2);
	} else {
		dma_dscr_idx = CAPTURE_END_DMA_DESCR_CH15;
		/* dmadscr[1].src is unused by hardware. Assgned to 0 to
		 * remove compiler warning */
		dmadscr[1].src = 0;
		dmadscr[1].dest = dmadscr[0].dest + (size / 2);
		dmadscr[1].xfer_val |= (ENABLE << 22) |
					(FROM_ACP_I2S_1 << 16) | (size / 2);
	}

	config_dma_descriptor_in_sram(acp_mmio, dma_dscr_idx, &dmadscr[1]);

	/* Configure the DMA channel with the above descriptore */
	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		/* starting descriptor for this channel */
		dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH13;
		config_acp_dma_channel(acp_mmio, ACP_TO_I2S_DMA_CH_NUM,
					dma_dscr_idx, num_descr,
					ACP_DMA_PRIORITY_LEVEL_NORMAL);
	} else {
		/* starting descriptor for this channel */
		dma_dscr_idx = CAPTURE_START_DMA_DESCR_CH15;
		config_acp_dma_channel(acp_mmio, I2S_TO_ACP_DMA_CH_NUM,
					dma_dscr_idx, num_descr,
					ACP_DMA_PRIORITY_LEVEL_NORMAL);
	}

}

u16 get_dscr_idx(void __iomem *acp_mmio, int direction)
{
	u16 dscr_idx;

	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		dscr_idx = acp_reg_read(acp_mmio, mmACP_DMA_CUR_DSCR_13);
		dscr_idx = (dscr_idx == PLAYBACK_START_DMA_DESCR_CH13) ?
				PLAYBACK_START_DMA_DESCR_CH12 :
				PLAYBACK_END_DMA_DESCR_CH12;
	} else {
		dscr_idx = acp_reg_read(acp_mmio, mmACP_DMA_CUR_DSCR_15);
		dscr_idx = (dscr_idx == CAPTURE_START_DMA_DESCR_CH15) ?
				CAPTURE_END_DMA_DESCR_CH14 :
				CAPTURE_START_DMA_DESCR_CH14;
	}

	return dscr_idx;
}

/* Create page table entries in ACP SRAM for the allocated memory */
static void acp_pte_config(void __iomem *acp_mmio, struct page *pg,
			   u16 num_of_pages, u32 pte_offset)
{
	u16 page_idx;
	u64 addr;
	u32 low;
	u32 high;
	u32 offset;

	offset	= ACP_DAGB_GRP_SRBM_SRAM_BASE_OFFSET + (pte_offset * 8);
	for (page_idx = 0; page_idx < (num_of_pages); page_idx++) {
		/* Load the low address of page int ACP SRAM through SRBM */
		acp_reg_write((offset + (page_idx * 8)),
			acp_mmio, mmACP_SRBM_Targ_Idx_Addr);
		addr = page_to_phys(pg);

		low = lower_32_bits(addr);
		high = upper_32_bits(addr);

		acp_reg_write(low, acp_mmio, mmACP_SRBM_Targ_Idx_Data);

		/* Load the High address of page int ACP SRAM through SRBM */
		acp_reg_write((offset + (page_idx * 8) + 4),
			acp_mmio, mmACP_SRBM_Targ_Idx_Addr);

		/* page enable in ACP */
		high |= BIT(31);
		acp_reg_write(high, acp_mmio, mmACP_SRBM_Targ_Idx_Data);

		/* Move to next physically contiguos page */
		pg++;
	}
}

/* enables/disables ACP's external interrupt */
void acp_enable_external_interrupts(void __iomem *acp_mmio,
					   int enable)
{
	u32 acp_ext_intr_enb;

	acp_ext_intr_enb = enable ?
				ACP_EXTERNAL_INTR_ENB__ACPExtIntrEnb_MASK : 0;

	/* Write the Software External Interrupt Enable register */
	acp_reg_write(acp_ext_intr_enb, acp_mmio, mmACP_EXTERNAL_INTR_ENB);
}

/* Clear (acknowledge) DMA 'Interrupt on Complete' (IOC) in ACP
 * external interrupt status register
 */
void acp_ext_stat_clear_dmaioc(void __iomem *acp_mmio, u8 ch_num)
{
	u32 ext_intr_stat;
	u32 chmask = BIT(ch_num);

	ext_intr_stat = acp_reg_read(acp_mmio, mmACP_EXTERNAL_INTR_STAT);
	if (ext_intr_stat & (chmask <<
			     ACP_EXTERNAL_INTR_STAT__DMAIOCStat__SHIFT)) {

		ext_intr_stat &= (chmask <<
				  ACP_EXTERNAL_INTR_STAT__DMAIOCAck__SHIFT);
		acp_reg_write(ext_intr_stat, acp_mmio,
						mmACP_EXTERNAL_INTR_STAT);
	}
}

/* Check whether ACP DMA interrupt (IOC) is generated or not */
u32 acp_get_intr_flag(void __iomem *acp_mmio)
{
	u32 ext_intr_status;
	u32 intr_gen;

	ext_intr_status = acp_reg_read(acp_mmio, mmACP_EXTERNAL_INTR_STAT);
	intr_gen = (((ext_intr_status &
		      ACP_EXTERNAL_INTR_STAT__DMAIOCStat_MASK) >>
		     ACP_EXTERNAL_INTR_STAT__DMAIOCStat__SHIFT));

	return intr_gen;
}

void config_acp_dma(void __iomem *acp_mmio,
			   struct audio_substream_data *audio_config)
{
	u32 pte_offset;

	if (audio_config->direction == SNDRV_PCM_STREAM_PLAYBACK)
		pte_offset = PLAYBACK_PTE_OFFSET;
	else
		pte_offset = CAPTURE_PTE_OFFSET;

	acp_pte_config(acp_mmio, audio_config->pg, audio_config->num_of_pages,
			pte_offset);

	/* Configure System memory <-> ACP SRAM DMA descriptors */
	set_acp_sysmem_dma_descriptors(acp_mmio, audio_config->size,
				       audio_config->direction, pte_offset);

	/* Configure ACP SRAM <-> I2S DMA descriptors */
	set_acp_to_i2s_dma_descriptors(acp_mmio, audio_config->size,
					audio_config->direction);
}

/* Start a given DMA channel transfer */
void acp_dma_start(void __iomem *acp_mmio,
			 u16 ch_num, bool is_circular)
{
	u32 dma_ctrl;

	/* read the dma control register and disable the channel run field */
	dma_ctrl = acp_reg_read(acp_mmio, mmACP_DMA_CNTL_0 + ch_num);

	/*Invalidating the DAGB cache */
	acp_reg_write(ENABLE, acp_mmio, mmACP_DAGB_ATU_CTRL);

	/* configure the DMA channel and start the DMA transfer
	 * set dmachrun bit to start the transfer and enable the
	 * interrupt on completion of the dma transfer
	 */
	dma_ctrl |= ACP_DMA_CNTL_0__DMAChRun_MASK;

	if ((ch_num == ACP_TO_I2S_DMA_CH_NUM) ||
	    (ch_num == ACP_TO_SYSRAM_CH_NUM) ||
	    (ch_num == I2S_TO_ACP_DMA_CH_NUM))
		dma_ctrl |= ACP_DMA_CNTL_0__DMAChIOCEn_MASK;
	else
		dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChIOCEn_MASK;

	/* enable  for ACP SRAM to/from I2S DMA channel */
	if (is_circular == true)
		dma_ctrl |= ACP_DMA_CNTL_0__Circular_DMA_En_MASK;
	else
		dma_ctrl &= ~ACP_DMA_CNTL_0__Circular_DMA_En_MASK;

	acp_reg_write(dma_ctrl, acp_mmio, mmACP_DMA_CNTL_0 + ch_num);
}

/* Stop a given DMA channel transfer */
void acp_dma_stop(void __iomem *acp_mmio, u8 ch_num)
{
	u32 dma_ctrl;
	u32 dma_ch_sts;
	u32 delay_time = ACP_DMA_RESET_TIME;

	dma_ctrl = acp_reg_read(acp_mmio, mmACP_DMA_CNTL_0 + ch_num);

	/* clear the dma control register fields before writing zero
	 * in reset bit
	 */
	dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChRun_MASK;
	dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChIOCEn_MASK;

	acp_reg_write(dma_ctrl, acp_mmio, mmACP_DMA_CNTL_0 + ch_num);
	dma_ch_sts = acp_reg_read(acp_mmio, mmACP_DMA_CH_STS);

	if (dma_ch_sts & BIT(ch_num)) {
		/* set the reset bit for this channel
		 * to stop the dma transfer */
		dma_ctrl |= ACP_DMA_CNTL_0__DMAChRst_MASK;
		acp_reg_write(dma_ctrl, acp_mmio, mmACP_DMA_CNTL_0 + ch_num);
	}

	/* check the channel status bit for some time and return the status */
	while (0 < delay_time) {
		dma_ch_sts = acp_reg_read(acp_mmio, mmACP_DMA_CH_STS);
		if (!(dma_ch_sts & BIT(ch_num))) {
			/* clear the reset flag after successfully stopping
			   the dma transfer and break from the loop */
			dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChRst_MASK;

			acp_reg_write(dma_ctrl, acp_mmio, mmACP_DMA_CNTL_0
								+ ch_num);
			break;
		}
		delay_time--;
	}
}

/* power off a tile/block within ACP */
static void acp_suspend_tile(void __iomem *acp_mmio, int tile)
{
	u32 val = 0;
	u32 timeout = 0;

	if ((tile  < ACP_TILE_P1) || (tile > ACP_TILE_DSP2))
		return;

	val = acp_reg_read(acp_mmio, mmACP_PGFSM_READ_REG_0 + tile);
	val &= ACP_TILE_ON_MASK;

	if (val == 0x0) {
		val = acp_reg_read(acp_mmio, mmACP_PGFSM_RETAIN_REG);
		val = val | (1 << tile);
		acp_reg_write(val, acp_mmio, mmACP_PGFSM_RETAIN_REG);
		acp_reg_write(0x500 + tile, acp_mmio, mmACP_PGFSM_CONFIG_REG);

		timeout = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
		while (timeout--) {
			val = acp_reg_read(acp_mmio, mmACP_PGFSM_READ_REG_0
								+ tile);
			val = val & ACP_TILE_ON_MASK;
			if (val == ACP_TILE_OFF_MASK)
				break;
		}

		val = acp_reg_read(acp_mmio, mmACP_PGFSM_RETAIN_REG);

		val |= ACP_TILE_OFF_RETAIN_REG_MASK;
		acp_reg_write(val, acp_mmio, mmACP_PGFSM_RETAIN_REG);
	}
}

/* power on a tile/block within ACP */
static void acp_resume_tile(void __iomem *acp_mmio, int tile)
{
	u32 val = 0;
	u32 timeout = 0;

	if ((tile  < ACP_TILE_P1) || (tile > ACP_TILE_DSP2))
		return;

	val = acp_reg_read(acp_mmio, mmACP_PGFSM_READ_REG_0 + tile);
	val = val & ACP_TILE_ON_MASK;

	if (val != 0x0) {
		acp_reg_write(0x600 + tile, acp_mmio, mmACP_PGFSM_CONFIG_REG);
		timeout = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
		while (timeout--) {
			val = acp_reg_read(acp_mmio, mmACP_PGFSM_READ_REG_0
							+ tile);
			val = val & ACP_TILE_ON_MASK;
			if (val == 0x0)
				break;
		}
		val = acp_reg_read(acp_mmio, mmACP_PGFSM_RETAIN_REG);
		if (tile == ACP_TILE_P1)
			val = val & (ACP_TILE_P1_MASK);
		else if (tile == ACP_TILE_P2)
			val = val & (ACP_TILE_P2_MASK);

		acp_reg_write(val, acp_mmio, mmACP_PGFSM_RETAIN_REG);
	}
}

/* Shutdown unused SRAM memory banks in ACP IP */
static void acp_turnoff_sram_banks(void __iomem *acp_mmio)
{
	/* Bank 0 : used for DMA descriptors
	 * Bank 1 to 4 : used for playback
	 * Bank 5 to 8 : used for capture
	 * Each bank is 8kB and max size allocated for playback/ capture is
	 * 16kB(max period size) * 2(max periods) reserved for playback/capture
	 * in ALSA driver
	 * Turn off all SRAM banks except above banks during playback/capture
	 */
	u32 val, bank;

	for (bank = 9; bank < 32; bank++) {
		val = acp_reg_read(acp_mmio, mmACP_MEM_SHUT_DOWN_REQ_LO);
		if (!(val & (1 << bank))) {
			val |= 1 << bank;
			acp_reg_write(val, acp_mmio,
					mmACP_MEM_SHUT_DOWN_REQ_LO);
			/* If ACP_MEM_SHUT_DOWN_STS_LO is 0xFFFFFFFF, then
			 * shutdown sequence is complete. */
			do {
				val = acp_reg_read(acp_mmio,
						mmACP_MEM_SHUT_DOWN_STS_LO);
			} while (val != 0xFFFFFFFF);
		}
	}

	for (bank = 32; bank < 48; bank++) {
		val = acp_reg_read(acp_mmio, mmACP_MEM_SHUT_DOWN_REQ_HI);
		if (!(val & (1 << (bank - 32)))) {
			val |= 1 << (bank - 32);
			acp_reg_write(val, acp_mmio,
					mmACP_MEM_SHUT_DOWN_REQ_HI);
			/* If ACP_MEM_SHUT_DOWN_STS_HI is 0x0000FFFF, then
			 * shutdown sequence is complete. */
			do {
				val = acp_reg_read(acp_mmio,
						mmACP_MEM_SHUT_DOWN_STS_HI);
			} while (val != 0x0000FFFF);
		}
	}
}

/* Initialize and bring ACP hardware to default state. */
static void acp_init(void __iomem *acp_mmio)
{
	u32 val;
	u32 timeout_value;

	/* Assert Soft reset of ACP */
	val = acp_reg_read(acp_mmio, mmACP_SOFT_RESET);

	val |= ACP_SOFT_RESET__SoftResetAud_MASK;
	acp_reg_write(val, acp_mmio, mmACP_SOFT_RESET);

	timeout_value = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
	while (timeout_value--) {
		val = acp_reg_read(acp_mmio, mmACP_SOFT_RESET);
		if (ACP_SOFT_RESET__SoftResetAudDone_MASK ==
		    (val & ACP_SOFT_RESET__SoftResetAudDone_MASK))
			break;
	}

	/* Enable clock to ACP and wait until the clock is enabled */
	val = acp_reg_read(acp_mmio, mmACP_CONTROL);
	val = val | ACP_CONTROL__ClkEn_MASK;
	acp_reg_write(val, acp_mmio, mmACP_CONTROL);

	timeout_value = ACP_CLOCK_EN_TIME_OUT_VALUE;

	while (timeout_value--) {
		val = acp_reg_read(acp_mmio, mmACP_STATUS);
		if (val & (u32) 0x1)
			break;
		udelay(100);
	}

	/* Deassert the SOFT RESET flags */
	val = acp_reg_read(acp_mmio, mmACP_SOFT_RESET);
	val &= ~ACP_SOFT_RESET__SoftResetAud_MASK;
	acp_reg_write(val, acp_mmio, mmACP_SOFT_RESET);

	/* initiailizing Garlic Control DAGB register */
	acp_reg_write(ONION_CNTL_DEFAULT, acp_mmio, mmACP_AXI2DAGB_ONION_CNTL);

	/* initiailizing Onion Control DAGB registers */
	acp_reg_write(GARLIC_CNTL_DEFAULT, acp_mmio,
			mmACP_AXI2DAGB_GARLIC_CNTL);

	acp_dma_descr_init(acp_mmio);

	acp_reg_write(ACP_SRAM_BASE_ADDRESS, acp_mmio,
			mmACP_DMA_DESC_BASE_ADDR);

	/* Num of descriptiors in SRAM 0x4, means 256 descriptors;(64 * 4) */
	acp_reg_write(0x4, acp_mmio, mmACP_DMA_DESC_MAX_NUM_DSCR);
	acp_reg_write(ACP_EXTERNAL_INTR_CNTL__DMAIOCMask_MASK,
		acp_mmio, mmACP_EXTERNAL_INTR_CNTL);

	acp_turnoff_sram_banks(acp_mmio);
}

void acp_hw_init(void __iomem *acp_mmio)
{
	acp_init(acp_mmio);

	/* Disable DSPs which are not used */
	acp_suspend_tile(acp_mmio, ACP_TILE_DSP0);
	acp_suspend_tile(acp_mmio, ACP_TILE_DSP1);
	acp_suspend_tile(acp_mmio, ACP_TILE_DSP2);
}

/* Deintialize ACP */
void acp_hw_deinit(void __iomem *acp_mmio)
{
	u32 val;
	u32 timeout_value;

	/* Assert Soft reset of ACP */
	val = acp_reg_read(acp_mmio, mmACP_SOFT_RESET);

	val |= ACP_SOFT_RESET__SoftResetAud_MASK;
	acp_reg_write(val, acp_mmio, mmACP_SOFT_RESET);

	timeout_value = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
	while (timeout_value--) {
		val = acp_reg_read(acp_mmio, mmACP_SOFT_RESET);
		if (ACP_SOFT_RESET__SoftResetAudDone_MASK ==
		    (val & ACP_SOFT_RESET__SoftResetAudDone_MASK)) {
			break;
	    }
	}
	/** Disable ACP clock */
	val = acp_reg_read(acp_mmio, mmACP_CONTROL);
	val &= ~ACP_CONTROL__ClkEn_MASK;
	acp_reg_write(val, acp_mmio, mmACP_CONTROL);

	timeout_value = ACP_CLOCK_EN_TIME_OUT_VALUE;

	while (timeout_value--) {
		val = acp_reg_read(acp_mmio, mmACP_STATUS);
		if (!(val & (u32) 0x1))
			break;
		udelay(100);
	}
}

/* Update DMA postion in audio ring buffer at period level granularity.
 * This will be used by ALSA PCM driver
 */
u32 acp_update_dma_pointer(void __iomem *acp_mmio, int direction,
				  u32 period_size)
{
	u32 pos;
	u16 dscr;
	u32 mul;
	u32 dma_config;

	pos = 0;

	if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
		dscr = acp_reg_read(acp_mmio, mmACP_DMA_CUR_DSCR_13);

		mul = (dscr == PLAYBACK_START_DMA_DESCR_CH13) ? 0 : 1;
		pos =  (mul * period_size);

	} else {
		dma_config = acp_reg_read(acp_mmio, mmACP_DMA_CNTL_14);
		if (dma_config != 0) {
			dscr = acp_reg_read(acp_mmio, mmACP_DMA_CUR_DSCR_14);
			mul = (dscr == CAPTURE_START_DMA_DESCR_CH14) ? 1 : 2;
			pos = (mul * period_size);
		}

		if (pos >= (2 * period_size))
			pos = 0;

	}
	return pos;
}

/* Wait for initial buffering to complete in HOST to SRAM DMA channel
 * for plaback usecase
 */
void prebuffer_audio(void __iomem *acp_mmio)
{
	u32 dma_ch_sts;
	u32 channel_mask = BIT(SYSRAM_TO_ACP_CH_NUM);

	do {
		/* Read the channel status to poll dma transfer completion
		 * (System RAM to SRAM)
		 * In this case, it will be runtime->start_threshold
		 * (2 ALSA periods) of transfer. Rendering starts after this
		 * threshold is met.
		 */
		dma_ch_sts = acp_reg_read(acp_mmio, mmACP_DMA_CH_STS);
		udelay(20);
	} while (dma_ch_sts & channel_mask);
}

void acp_suspend(void __iomem *acp_mmio)
{
	acp_suspend_tile(acp_mmio, ACP_TILE_P2);
	acp_suspend_tile(acp_mmio, ACP_TILE_P1);
}

void acp_resume(void __iomem *acp_mmio)
{
	acp_resume_tile(acp_mmio, ACP_TILE_P1);
	acp_resume_tile(acp_mmio, ACP_TILE_P2);

	acp_init(acp_mmio);

	/* Disable DSPs which are not going to be used */
	acp_suspend_tile(acp_mmio, ACP_TILE_DSP0);
	acp_suspend_tile(acp_mmio, ACP_TILE_DSP1);
	acp_suspend_tile(acp_mmio, ACP_TILE_DSP2);
}
