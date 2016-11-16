/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include "hdcpss.h"
#include "hdcpss_interface.h"
#include "amdgpu_psp_if.h"
#include "amdgpu.h"

#include "hdcp_types.h"

#define FIRMWARE_CARRIZO	"amdgpu/hdcp14tx_ta.bin"
#define ASD_BIN_CARRIZO		"amdgpu/asd.bin"
MODULE_FIRMWARE(FIRMWARE_CARRIZO);
MODULE_FIRMWARE(ASD_BIN_CARRIZO);

int hdcpss_notify_ta(struct hdcpss_data *);
int hdcpss_send_close_session(struct hdcpss_data *, struct dm_hdcp_info *info);
void hdcpss_notify_hotplug_detect(struct amdgpu_device *, int, void *);

int hdcpss_read_An_Aksv(
	struct hdcpss_data *hdcp,
	struct dm_hdcp_info *info)
{
	int ret = 0;
	int i = 0;

	memset(hdcp->tci_buf_addr, 0, sizeof(HDCP_TCI));

	hdcp->tci_buf_addr->HDCP_14_Message.CommandHeader.
		commandId = HDCP_CMD_HOST_CMDS;
	hdcp->tci_buf_addr->eHDCPSessionType = HDCP_14;
	hdcp->tci_buf_addr->eHDCPCommand = TL_HDCP_CMD_ID_OPEN_SESSION;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.DigId = info->link_enc_hw_inst;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.OpenSession.bIsDualLink = 0;

	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.OpenSession.DDCLine = info->ddc_hw_inst;

	dev_dbg(hdcp->adev->dev, "DDCLine = %x\n",
					hdcp->tci_buf_addr->HDCP_14_Message.
					CmdHDCPCmdInput.OpenSession.DDCLine);

	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.OpenSession.Bcaps = hdcp->Bcaps;

	dev_dbg(hdcp->adev->dev, "BCaps = %x\n",
					hdcp->tci_buf_addr->HDCP_14_Message.
					CmdHDCPCmdInput.OpenSession.Bcaps);

	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.OpenSession.ConnectorType =
					hdcp->connector_type;

	dev_dbg(hdcp->adev->dev, "Connector Type = %x\n",
				hdcp->tci_buf_addr->HDCP_14_Message.
				CmdHDCPCmdInput.OpenSession.ConnectorType);

	dev_info(hdcp->adev->dev,
			"Sending command TL_HDCP_CMD_ID_OPEN_SESSION\n");

	ret = hdcpss_notify_ta(hdcp);

	if (1 != hdcp->tci_buf_addr->HDCP_14_Message.ResponseHeader.
			returnCode) {
		dev_err(hdcp->adev->dev, "Error from Trustlet = %d\n",
				hdcp->tci_buf_addr->HDCP_14_Message.
				ResponseHeader.returnCode);
		ret = 1;
	}
	dev_info(hdcp->adev->dev, "TA return code = %d\n",
			hdcp->tci_buf_addr->HDCP_14_Message.
			ResponseHeader.returnCode);
	dev_info(hdcp->adev->dev, "TA response code = %d\n",
			hdcp->tci_buf_addr->HDCP_14_Message.
			RspHDCPCmdOutput.bResponseCode);

	if (!ret) {
		hdcp->Ainfo = hdcp->tci_buf_addr->
				HDCP_14_Message.RspHDCPCmdOutput.
				OpenSession.AInfo;
		dev_dbg(hdcp->adev->dev, "Ainfo = %x\n",
				hdcp->tci_buf_addr->
				HDCP_14_Message.RspHDCPCmdOutput.
				OpenSession.AInfo);
		if(hdcp->is_primary_link) {
			dev_info(hdcp->adev->dev,
					"An received :\n");
			for (i = 0; i < 8; i++) {
				dev_info(hdcp->adev->dev, "%x\t",
						hdcp->tci_buf_addr->
						HDCP_14_Message.RspHDCPCmdOutput.
						OpenSession.AnPrimary[i]);
			}
			memcpy(hdcp->AnPrimary, hdcp->tci_buf_addr->HDCP_14_Message.
                                RspHDCPCmdOutput.OpenSession.AnPrimary,
                                8);

			dev_info(hdcp->adev->dev, "Aksv received :\n");
			for (i = 0; i < 5; i++) {
				dev_info(hdcp->adev->dev, "%x\t",
						hdcp->tci_buf_addr->
						HDCP_14_Message.RspHDCPCmdOutput.
						OpenSession.AksvPrimary[i]);
			}
			memcpy(hdcp->AksvPrimary, hdcp->tci_buf_addr->HDCP_14_Message.
                                RspHDCPCmdOutput.OpenSession.AksvPrimary,
                                5);
		} else {
			dev_info(hdcp->adev->dev,
					"An Secondary received :\n");
			for (i = 0; i < 8; i++) {
				dev_info(hdcp->adev->dev,
						"%x\t",
						hdcp->tci_buf_addr->
						HDCP_14_Message.
						RspHDCPCmdOutput.
						OpenSession.
						AnSecondary[i]);
			}
			dev_info(hdcp->adev->dev,
					"Aksv received :\n");
			for (i = 0; i < 5; i++) {
				dev_info(hdcp->adev->dev,
						"%x\t",
						hdcp->tci_buf_addr->
						HDCP_14_Message
						.RspHDCPCmdOutput.
						OpenSession
						.AksvSecondary[i]);
			}
		}
	}
	return ret;
}

bool hdcpss_write_Ainfo(struct hdcpss_data *hdcp, void *display_id)
{
	struct hdcp_protection_message message;
	bool ret = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_WRITE_AINFO;
	message.length = sizeof(uint8_t);
	message.data = &hdcp->Ainfo;

	dev_dbg(hdcp->adev->dev, "Writing Ainfo = %x\n", hdcp->Ainfo);

	ret = amdgpu_dm_process_hdcp_msg(display_id, &message);

	return ret;
}

bool hdcpss_write_An(struct hdcpss_data *hdcp, void *display_id)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	if (hdcp->is_primary_link)
		message.link = HDCP_LINK_PRIMARY;
	else
		message.link = HDCP_LINK_SECONDARY;

	message.version = HDCP_VERSION_14;
	message.msg_id = HDCP_MESSAGE_ID_WRITE_AN;
	message.length = 8;
	message.data = hdcp->AnPrimary;

	for(i = 0; i < 8; i++)
		dev_dbg(hdcp->adev->dev, "%s An[%d] = %x\n", __func__, i,
					hdcp->AnPrimary[i]);

	ret = amdgpu_dm_process_hdcp_msg( display_id, &message);

	return ret;
}

bool hdcpss_write_Aksv(struct hdcpss_data *hdcp, void *display_id)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	if (hdcp->is_primary_link)
		message.link = HDCP_LINK_PRIMARY;
	else
		message.link = HDCP_LINK_SECONDARY;

	message.version = HDCP_VERSION_14;
	message.msg_id = HDCP_MESSAGE_ID_WRITE_AKSV;
	message.length = 5;
	message.data = hdcp->AksvPrimary;

	for(i = 0; i < 5; i++)
		dev_dbg(hdcp->adev->dev, "%s AKSV[%d] = %x\n", __func__, i,
							hdcp->AksvPrimary[i]);

	ret = amdgpu_dm_process_hdcp_msg( display_id, &message);

	return ret;
}

bool hdcpss_read_Bksv(struct hdcpss_data *hdcp, void *display_id, u32 link_type)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	message.version = HDCP_VERSION_14;
	message.link = link_type;
	message.msg_id = HDCP_MESSAGE_ID_READ_BKSV;
	message.length = 5;
	if(link_type == HDCP_LINK_PRIMARY)
		message.data = hdcp->BksvPrimary;
	else {
		message.data = hdcp->BksvSecondary;
	}

	ret = amdgpu_dm_process_hdcp_msg( display_id, &message);

	if (ret) {
		dev_info(hdcp->adev->dev, "Received BKsv\n");
		for (i = 0; i < 5; i++)
			dev_info(hdcp->adev->dev, "BKsv[%d] = %x\n", i,
							hdcp->BksvPrimary[i]);
	}

	return ret;
}

bool hdcpss_read_Bcaps(struct hdcpss_data *hdcp, void *display_id)
{
	struct hdcp_protection_message message;
	bool ret;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_BCAPS;
	message.length = 1;
	message.data = &hdcp->Bcaps;

	ret = amdgpu_dm_process_hdcp_msg( display_id, &message);

	if (!ret)
		return ret;

	dev_info(hdcp->adev->dev, "Received BCaps = %x\n", hdcp->Bcaps);

	if (hdcp->connector_type == HDCP_14_CONNECTOR_TYPE_HDMI) {
		/* Check if Repeater bit is set */
		if (hdcp->Bcaps & (1 << BIT_REPEATER)) {
			dev_info(hdcp->adev->dev,
				"HDMI: Connected Receiver is also a Repeater\n");
			hdcp->is_repeater = 1;
		}
	} else {
		/* DP Case */
		if (hdcp->Bcaps & (1 << BIT_DP_REPEATER)) {
			dev_info(hdcp->adev->dev,
				"DP: Connected Receiver is also a Repeater\n");
			hdcp->is_repeater = 1;
		}
	}

	return ret;
}

bool hdcpss_read_R0not(struct hdcpss_data *hdcp, void *display_id)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_RI_R0;
	message.length = 2;
	message.data = hdcp->R_Prime;

	ret = amdgpu_dm_process_hdcp_msg( display_id, &message);

	if (ret) {
		dev_info(hdcp->adev->dev, "Received R0 prime\n");
		for (i = 0; i < 2; i++)
			dev_info(hdcp->adev->dev, "R0_Prime[%d] = %x\n", i,
							hdcp->R_Prime[i]);
	}

	return ret;
}

bool hdcpss_read_Pj(struct hdcpss_data *hdcp, void *display_id)
{
	struct hdcp_protection_message message;
	bool ret = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_PJ;
	message.length = 1;
	message.data = &hdcp->Pj;

	ret = amdgpu_dm_process_hdcp_msg(display_id, &message);

	if (ret)
		dev_info(hdcp->adev->dev, "Received Pj = %x\n", hdcp->Pj);

	return ret;
}

bool hdcpss_read_Binfo(struct hdcpss_data *hdcp, void *display_id)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_BINFO;
	message.length = 2;
	message.data = hdcp->Binfo;

	ret = amdgpu_dm_process_hdcp_msg(display_id, &message);
	if (ret) {
		dev_info(hdcp->adev->dev, "Received BInfo\n");
		for (i = 0; i < 2; i++)
			dev_info(hdcp->adev->dev, "BInfo[%d] = %x\n", i,
							hdcp->Binfo[i]);
	}
	return ret;
}

bool hdcpss_read_bstatus_dp(struct hdcpss_data *hdcp, void *display_id)
{
	struct hdcp_protection_message message;
	bool ret = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_BSTATUS;
	message.length = 1;
	message.data = &hdcp->bstatus_dp;

	ret = amdgpu_dm_process_hdcp_msg(display_id, &message);
	if (ret)
		dev_info(hdcp->adev->dev, "BStatus_DP = %x\n",
							hdcp->bstatus_dp);

	return ret;
}


int hdcpss_send_first_part_auth(struct hdcpss_data *hdcp,
					struct dm_hdcp_info *info,
					u32 hdcp_link_type)
{
	int ret = 0;
	int i = 0;

	memset(hdcp->tci_buf_addr, 0, sizeof(HDCP_TCI));

	hdcp->tci_buf_addr->HDCP_14_Message.CommandHeader
				.commandId = HDCP_CMD_HOST_CMDS;
	hdcp->tci_buf_addr->eHDCPSessionType = HDCP_14;
	hdcp->tci_buf_addr->eHDCPCommand =
			TL_HDCP_CMD_ID_HDCP_14_FIRST_PART_AUTH;
	hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.DigId = info->link_enc_hw_inst;

	if (hdcp_link_type  == HDCP_LINK_PRIMARY)
		memcpy(hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
						FirstPartAuth.BksvPrimary,
						&hdcp->BksvPrimary, 5);
	else
		memcpy(hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
						FirstPartAuth.BksvSecondary,
						&hdcp->BksvSecondary, 5);

	hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
					FirstPartAuth.Bcaps = hdcp->Bcaps;

	memcpy(hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
				FirstPartAuth.RNotPrime, &hdcp->R_Prime,
				sizeof(uint16_t));

	dev_info(hdcp->adev->dev, "DigID = %x\n",
		hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.DigId);

	for (i = 0; i < 5; i++)
		dev_dbg(hdcp->adev->dev, "BKSV Primary [%x] = %x\n", i,
			hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
						FirstPartAuth.BksvPrimary[i]);

	dev_dbg(hdcp->adev->dev, "Bcaps = %x\n",
			hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
			FirstPartAuth.Bcaps);

	for (i = 0; i < 2; i++)
		dev_dbg(hdcp->adev->dev, "R_Prime [%x] = %x\n", i,
			hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
			FirstPartAuth.RNotPrime[i]);

	dev_info(hdcp->adev->dev,
		"Sending command TL_HDCP_CMD_ID_HDCP_14_FIRST_PART_AUTH\n");

	ret = hdcpss_notify_ta(hdcp);

	if (1 != hdcp->tci_buf_addr->HDCP_14_Message.ResponseHeader.
			returnCode) {
		dev_err(hdcp->adev->dev, "Error from Trustlet = %d\n",
				hdcp->tci_buf_addr->HDCP_14_Message.
				ResponseHeader.returnCode);
		ret = 1;
	}
	dev_info(hdcp->adev->dev, "TA return code = %d\n",
			hdcp->tci_buf_addr->HDCP_14_Message.
			ResponseHeader.returnCode);
	dev_info(hdcp->adev->dev, "TA response code = %d\n",
			hdcp->tci_buf_addr->HDCP_14_Message.
			RspHDCPCmdOutput.bResponseCode);

	return ret;
}

bool hdcpss_read_bstatus(struct hdcpss_data *hdcp,void *display_id)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_BSTATUS;
	message.length = 2;
	message.data = hdcp->bstatus;

	ret = amdgpu_dm_process_hdcp_msg(display_id, &message);
	if (ret) {
		dev_info(hdcp->adev->dev, "Received BStatus\n");
		for (i = 0; i < 2; i++)
			dev_info(hdcp->adev->dev, "BStatus[%d] = %x\n", i,
							hdcp->bstatus[i]);
	}

	return ret;
}

bool hdcpss_read_ksv_fifo(struct hdcpss_data *hdcp, void *display_id,
						u32 device_count)
{
	struct hdcp_protection_message message;

	const int ksv_fifo_window_size = 15;
	bool ret = false;
	int i;
	int reads_num;
	int left_to_read;

	hdcp->ksv_list_size = device_count * 5;

	/* TODO: Free this memory when not needed */
	hdcp->ksv_fifo_buf = kmalloc(hdcp->ksv_list_size, GFP_KERNEL);
	if (!hdcp->ksv_fifo_buf) {
		dev_err(hdcp->adev->dev, "memory allocation failure\n");
		hdcp->ksv_list_size = 0;
		return false;
	}
	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_KSV_FIFO;

	if (hdcp->connector_type == HDCP_14_CONNECTOR_TYPE_DP) {
		left_to_read = hdcp->ksv_list_size;
		reads_num = hdcp->ksv_list_size / ksv_fifo_window_size + 1;

		for (i = 0; i < reads_num; i++) {
			message.length = min(left_to_read, ksv_fifo_window_size);
			message.data = hdcp->ksv_fifo_buf + i * 15;
			left_to_read -= message.length;

			ret = amdgpu_dm_process_hdcp_msg(display_id, &message);

			if (!ret)
				break;
		}
	} else {
		/* HDMI case */
		message.length = hdcp->ksv_list_size;
		message.data = hdcp->ksv_fifo_buf;
		ret = amdgpu_dm_process_hdcp_msg(display_id, &message);
	}
	if (ret) {
		dev_info(hdcp->adev->dev, "Received KSV FIFO data\n");
		for (i = 0; i < hdcp->ksv_list_size; i++)
			dev_info(hdcp->adev->dev, "KSV FIFO buf[%d] = %x\n", i,
							hdcp->ksv_fifo_buf[i]);
	}

	return ret;
}

bool hdcpss_read_v_prime(struct hdcpss_data *hdcp,
		void *display_id)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_VH_X;
	message.length = 20;
	message.data = hdcp->V_Prime;

	ret = amdgpu_dm_process_hdcp_msg(display_id, &message);
	if (ret) {
		dev_info(hdcp->adev->dev, "Received V Prime\n");
		for (i = 0; i < 20; i++)
			dev_info(hdcp->adev->dev, "V_Prime[%d] = %x\n", i,
							hdcp->V_Prime[i]);
	}

	return ret;
}

int hdcpss_send_second_part_auth(struct hdcpss_data *hdcp,
					struct dm_hdcp_info *info,
					u32 hdcp_link_type)
{
	int ret = 0;
	int i = 0;

	dev_info(hdcp->adev->dev, "%s: Started\n", __func__);

	memset(hdcp->tci_buf_addr, 0, sizeof(HDCP_TCI));

	hdcp->tci_buf_addr->HDCP_14_Message.CommandHeader.
		commandId = HDCP_CMD_HOST_CMDS;
	hdcp->tci_buf_addr->eHDCPSessionType = HDCP_14;
	hdcp->tci_buf_addr->eHDCPCommand = TL_HDCP_CMD_ID_HDCP_14_SECOND_PART_AUTH;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.DigId = info->link_enc_hw_inst;

	if (hdcp->connector_type == HDCP_14_CONNECTOR_TYPE_HDMI) {
		memcpy(hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
					SecondPartAuth.HdmiBStatus,
					&hdcp->bstatus, 2);
	} else {
		memcpy(hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
					SecondPartAuth.DpBStatus,
					&hdcp->bstatus_dp, 1);

		memcpy(hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
				SecondPartAuth.BInfo,
				&hdcp->Binfo, 2);

	}

	memcpy(hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
				SecondPartAuth.KSVList,
				hdcp->ksv_fifo_buf, hdcp->ksv_list_size);

	hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
				SecondPartAuth.KSVListSize =
							hdcp->ksv_list_size/5;

	hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
				SecondPartAuth.Pj = hdcp->Pj;

	memcpy(hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
				SecondPartAuth.VPrime,
				&hdcp->V_Prime, 20);

	if (hdcp->connector_type == HDCP_14_CONNECTOR_TYPE_HDMI) {
		for (i = 0; i < 2; i++)
			printk("TCI: HdmiBStatus [%d]= %x\n", i,
				hdcp->tci_buf_addr->HDCP_14_Message.
				CmdHDCPCmdInput.SecondPartAuth.HdmiBStatus[i]);
	} else {
		printk("TCI: DpBStatus [%d] = %x\n", 0,
				hdcp->tci_buf_addr->HDCP_14_Message.
				CmdHDCPCmdInput.SecondPartAuth.DpBStatus[0]);
	}

	for (i = 0; i < 2; i++)
		printk("TCI: BInfo [%d]= %x\n", i,
				hdcp->tci_buf_addr->HDCP_14_Message.
				CmdHDCPCmdInput.SecondPartAuth.BInfo[i]);

	for (i = 0; i < 5; i++)
		printk("TCI: KSV FIFO BUF [%d] = %x\n", i,
				hdcp->tci_buf_addr->HDCP_14_Message.
				CmdHDCPCmdInput.SecondPartAuth.KSVList[i]);

	printk("TCI: KSVListSize = %x\n", hdcp->tci_buf_addr->HDCP_14_Message.
				CmdHDCPCmdInput.SecondPartAuth.KSVListSize);

	printk("TCI: Pj = %x\n", hdcp->tci_buf_addr->HDCP_14_Message.
					CmdHDCPCmdInput.SecondPartAuth.Pj);

	for (i = 0; i < 20; i++)
		printk("TCI: V'[%d] = %x\n", i,
				hdcp->tci_buf_addr->HDCP_14_Message.
				CmdHDCPCmdInput.SecondPartAuth.VPrime[i]);
	dev_info(hdcp->adev->dev, "DigID = %x\n",
		hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.DigId);

	dev_info(hdcp->adev->dev,
		"Sending command TL_HDCP_CMD_ID_HDCP_14_SECOND_PART_AUTH\n");

	dev_info(hdcp->adev->dev,"Before sending command: Out resp code = %x\n",
					hdcp->tci_buf_addr->HDCP_14_Message.
					RspHDCPCmdOutput.bResponseCode);

	ret = hdcpss_notify_ta(hdcp);

	if (1 != hdcp->tci_buf_addr->HDCP_14_Message.ResponseHeader.
			returnCode) {
		dev_err(hdcp->adev->dev, "Error from Trustlet = %d\n",
				hdcp->tci_buf_addr->HDCP_14_Message.
				ResponseHeader.returnCode);
		ret = 1;
	}
	dev_info(hdcp->adev->dev, "TA return code = %d\n",
			hdcp->tci_buf_addr->HDCP_14_Message.
			ResponseHeader.returnCode);
	dev_info(hdcp->adev->dev, "TA response code = %d\n",
			hdcp->tci_buf_addr->HDCP_14_Message.
			RspHDCPCmdOutput.bResponseCode);

	return ret;
}

int hdcpss_get_encryption_level(
	struct hdcpss_data *hdcp,
	void *display_id,
	int *save_encryption_level)
{
	int ret = 0;
	int encryption_level = 0;
	int fail_info = 0;

	struct dm_hdcp_info hdcp_info;

	if (hdcp->session_id == 0)
		return -EINVAL;

	if (!amdgpu_dm_get_hdcp_info(display_id, &hdcp_info))
		return 1;

	memset(hdcp->tci_buf_addr, 0, sizeof(HDCP_TCI));

	hdcp->tci_buf_addr->HDCP_14_Message.CommandHeader.
		commandId = HDCP_CMD_HOST_CMDS;
	hdcp->tci_buf_addr->eHDCPSessionType = HDCP_14;
	hdcp->tci_buf_addr->eHDCPCommand = TL_HDCP_CMD_ID_GET_PROTECTION_LEVEL;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.DigId = hdcp_info.link_enc_hw_inst;

	hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.
					GetProtectionLevel.bIsDualLink = 0;

	dev_info(hdcp->adev->dev,
		"Sending command TL_HDCP_CMD_ID_GET_PROTECTION_LEVEL\n");

	printk("display_id = %p\n", display_id);
	printk("DigID = %x\n", hdcp->tci_buf_addr->HDCP_14_Message.
							CmdHDCPCmdInput.DigId);

	ret = hdcpss_notify_ta(hdcp);

	if (1 != hdcp->tci_buf_addr->HDCP_14_Message.ResponseHeader.
			returnCode) {
		dev_err(hdcp->adev->dev, "Error from Trustlet = %d\n",
				hdcp->tci_buf_addr->HDCP_14_Message.
				ResponseHeader.returnCode);
		ret = 1;
	}
	dev_info(hdcp->adev->dev, "TA return code = %d\n",
			hdcp->tci_buf_addr->HDCP_14_Message.
			ResponseHeader.returnCode);
	dev_info(hdcp->adev->dev, "TA response code = %d\n",
			hdcp->tci_buf_addr->HDCP_14_Message.
			RspHDCPCmdOutput.bResponseCode);

	encryption_level = hdcp->tci_buf_addr->HDCP_14_Message.RspHDCPCmdOutput.
					GetProtectionLevel.ProtectionLevel;

	fail_info = hdcp->tci_buf_addr->HDCP_14_Message.RspHDCPCmdOutput.
					GetProtectionLevel.HdcpAuthFailInfo;

	dev_info(hdcp->adev->dev, "Encryption level = %x\n", encryption_level);

	dev_info(hdcp->adev->dev, "Fail Info = %x\n", fail_info);

	if (ret == 0 && save_encryption_level)
		*save_encryption_level = encryption_level;

	return ret;
}

static int count_number_of_ones(uint8_t *buff)
{
	static uint8_t hex_to_1s_num[] = {
		0, /* 0 */
		1, /* 1 */
		1, /* 2 */
		2, /* 3 */
		1, /* 4 */
		2, /* 5 */
		2, /* 6 */
		3, /* 7 */
		1, /* 8 */
		2, /* 9 */
		2, /* A */
		3, /* B */
		2, /* C */
		3, /* D */
		3, /* E */
		4, /* F */
	};
	uint8_t i = 0;
	uint8_t count_of_1s = 0;

	for (i = 0; i < 5; ++i) {
		count_of_1s +=
			hex_to_1s_num[0xf & buff[i]] +
			hex_to_1s_num[(0xf0 & buff[i]) >> 4];
	}
	printk("Number of 1s in BKSV = %d\n", count_of_1s);

	return count_of_1s;
}

/*
 * This function will be called when a connect event is detected.
 * It starts the authentication of the receiver.
 */
static int hdcpss_start_hdcp14_authentication(struct hdcpss_data *hdcp, struct dm_hdcp_info *info)
{
	uint8_t count_of_ones = 0;
	int ret = 0;
	uint8_t device_count = 0;
	uint32_t connector_type = 0;
	int i = 0;
	int m = 0;
	int enc_level;

	/* TODO: Obtain link type from DAL */
	hdcp->is_primary_link = 1;
	hdcp->is_repeater = 0;

	if (hdcp->session_id == 0 || hdcp->asd_session_id == 0) {
		dev_err(hdcp->adev->dev, "Error loading f/w binaries on PSP\n");
		return -EINVAL;
	}

	hdcp->dig_id = info->link_enc_hw_inst;
	dev_dbg(hdcp->adev->dev, "dig_id : %x display_id : %p\n",
				hdcp->dig_id, info->display_id);
	hdcp->is_session_closed[info->link_enc_hw_inst] = 0;

	connector_type = info->signal;

	switch (connector_type) {
		case SIGNAL_TYPE_HDMI_TYPE_A:
			hdcp->connector_type = HDCP_14_CONNECTOR_TYPE_HDMI;
			dev_info(hdcp->adev->dev, "HDMI plug detected\n");
			break;
		case SIGNAL_TYPE_DISPLAY_PORT:
			hdcp->connector_type = HDCP_14_CONNECTOR_TYPE_DP;
			dev_info(hdcp->adev->dev, "DP plug detected\n");
			break;
		default:
			return ret;
	}

	/* Read BKsv from Receiver */
	ret = hdcpss_read_Bksv(hdcp, info->display_id, HDCP_LINK_PRIMARY);
	if (!ret) {
		dev_err(hdcp->adev->dev, "Error in reading bksv\n");

		/* Access HDCP port in an attempt to start authentication */
		for (m = 0; m <= 10; m++) {
			ret = hdcpss_read_Bksv(hdcp, info->display_id, HDCP_LINK_PRIMARY);
			if (!ret) {
				dev_err(hdcp->adev->dev,
						"Error in reading bksv\n");
				if (m == 10)
					return ret;
				} else {
					break;
				}
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(msecs_to_jiffies(1500));
		}
	}

	/* Read BCaps from Receiver */
	ret = hdcpss_read_Bcaps(hdcp, info->display_id);
	if (!ret) {
		dev_err(hdcp->adev->dev, "Error in reading bcaps\n");
		return ret;
	}

	if (hdcp->connector_type == HDCP_14_CONNECTOR_TYPE_DP) {
		if (hdcp->Bcaps & (1 << BIT_DP_HDCP_CAPABLE)) {
			dev_info(hdcp->adev->dev, "Receiver is HDCP capable\n");
		} else {
			dev_info(hdcp->adev->dev,
					"Receiver is not HDCP capable\n");
			return ret;
		}
	}

	count_of_ones = count_number_of_ones(hdcp->BksvPrimary);
	if (count_of_ones != 20) {

		/* Re-read one more time */
		ret = hdcpss_read_Bksv(hdcp, info->display_id, HDCP_LINK_PRIMARY);
		if (!ret) {
			printk("Error in reading bksv\n");
			return ret;
		}
		count_of_ones = count_number_of_ones(hdcp->BksvPrimary);
		if (count_of_ones != 20) {
			printk("Connected display is Not HDCP compliant\n");
			return ret;
		}
	}

	/* Send OPEN_SESSION command to TA */
	ret = hdcpss_read_An_Aksv(hdcp, info);
	if (ret) {
		dev_err(hdcp->adev->dev, "Error in reading An and Aksv:%d\n",
									ret);
		return ret;
	}

	/* Write Ainfo register for HDMI connector */
	if (hdcp->connector_type == HDCP_14_CONNECTOR_TYPE_HDMI) {
		dev_dbg(hdcp->adev->dev, "Writing Ainfo for connector %x\n",
				hdcp->connector_type);
		ret = hdcpss_write_Ainfo(hdcp, info->display_id);
	}

	/* Write An to Receiver */
	ret = hdcpss_write_An(hdcp, info->display_id);
	if (!ret) {
		dev_err(hdcp->adev->dev, "Error in writing An\n");
		return ret;
	}
	/* Write AKsv to Receiver */
	ret = hdcpss_write_Aksv(hdcp, info->display_id);
	if (!ret) {
		dev_err(hdcp->adev->dev, "Error in writing AKsv\n");
		return ret;
	}

	mdelay(100);
	for (i = 0; i < MAX_R0_READ_RETRIES; i++) {
		ret = hdcpss_read_R0not(hdcp, info->display_id);
		if (!ret) {
			dev_err(hdcp->adev->dev, "Error in reading r0not\n");
			return ret;
		}
		ret = hdcpss_send_first_part_auth(hdcp, info,
							HDCP_LINK_PRIMARY);
		if (ret) {
			dev_err(hdcp->adev->dev,
				"Error in first part of authentication\n");
			if (hdcp->reauth_r0) {
				if (i == (MAX_R0_READ_RETRIES - 1)) {
					hdcp->reauth_r0 = 0;
					mutex_unlock(&hdcp->hdcpss_mutex);
					hdcpss_notify_hotplug_detect(hdcp->adev,
							0,
							info->display_id);
					hdcpss_notify_hotplug_detect(hdcp->adev,
							1,
							info->display_id);
					/*
					 * Check if authentication succeeded in
					 * as part of re-attempt.
					 */
					hdcpss_get_encryption_level(hdcp,
							info->display_id,
							&enc_level);
					if (enc_level)
						ret = 0;
					mutex_lock(&hdcp->hdcpss_mutex);
				}
			} else {
				dev_err(hdcp->adev->dev,
                                "Error in authentication after re-attempt\n");
				/* Restore back attempts flag */
				hdcp->reauth_r0 = 1;
				return ret;
			}
		} else {
			break;
		}
	}

	if (ret)
		return ret;

	dev_info(hdcp->adev->dev, "First Part authentication success\n");

	ret = hdcpss_get_encryption_level(hdcp, info->display_id, NULL);

	/* Repeater Only */
	if (hdcp->is_repeater) {
		dev_info(hdcp->adev->dev, "Repeater detected\n");
		if (hdcp->connector_type == HDCP_14_CONNECTOR_TYPE_HDMI) {

			/* Poll for KSV List Ready every 1 second */
			/* 5 seconds timeout */
			for (i = 0; i < 5; i++) {
				ret = hdcpss_read_Bcaps(hdcp, info->display_id);
				if (!ret) {
					dev_err(hdcp->adev->dev,
						"Error in reading bcaps\n");
					return ret;
				}
				if (hdcp->Bcaps & (1 << BIT_FIFO_READY)) {
					dev_dbg(hdcp->adev->dev,
						"KSV FIFO Ready i = %x\n", i);
					break;
				}
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(msecs_to_jiffies(1000));
			}

			/*
			 * Abort authentication if KSV list not ready
			 * after 5 seconds timeout
			 */
			if  (!(hdcp->Bcaps & (1 << BIT_FIFO_READY))) {
				dev_err(hdcp->adev->dev,
					"KSV list not ready after 5 seconds\n");
				if (hdcp->reauth_timeout) {
					hdcp->reauth_timeout = 0;
					mutex_unlock(&hdcp->hdcpss_mutex);
					hdcpss_notify_hotplug_detect(hdcp->adev,
							0,
							info->display_id);
					hdcpss_notify_hotplug_detect(hdcp->adev,
							1,
							info->display_id);
					mutex_lock(&hdcp->hdcpss_mutex);
				} else {
					dev_err(hdcp->adev->dev,
					"Reauthentication attempt failed\n");
					hdcp->reauth_timeout = 1;
					hdcp->ksv_timeout_err = 1;
					return -ETIMEDOUT;
				}
			}

			if (hdcp->ksv_timeout_err) {
				dev_err(hdcp->adev->dev, "Reattempt failed \n");
				ret = hdcpss_send_close_session(hdcp, info);
				if (ret) {
					dev_err(hdcp->adev->dev,
					"error in close session\n");
					return ret;
				}
				/*
				 * Setting flag to indicate session
				 * closed for particular dig_id
				 */
				hdcp->is_session_closed[info->link_enc_hw_inst] = 1;

				hdcp->ksv_timeout_err = 0;
				return ret;
			}

			/* Read Bstatus to know the DEVICE_COUNT */
			ret = hdcpss_read_bstatus(hdcp, info->display_id);
			if (!ret) {
				dev_err(hdcp->adev->dev,
						"Error in reading bstatus\n");
				return ret;
			}
			if (hdcp->bstatus[0] & (1 << BIT_MAX_DEVS_EXCEDDED)) {
				dev_err(hdcp->adev->dev,
					"Topology error: MAX_DEVS_EXCEDDED\n");
				mutex_unlock(&hdcp->hdcpss_mutex);
				hdcpss_notify_hotplug_detect(hdcp->adev,
							0,
							info->display_id);
				hdcpss_notify_hotplug_detect(hdcp->adev,
							1,
							info->display_id);
				mutex_lock(&hdcp->hdcpss_mutex);
			}

			if (hdcp->bstatus[1] &
					(1 << BIT_MAX_CASCADE_EXCEDDED)) {
				dev_err(hdcp->adev->dev,
				"Topology error: MAX_CASCADE_EXCEDDED\n");
				mutex_unlock(&hdcp->hdcpss_mutex);
				hdcpss_notify_hotplug_detect(hdcp->adev,
							0,
							info->display_id);
				hdcpss_notify_hotplug_detect(hdcp->adev,
							1,
							info->display_id);
				mutex_lock(&hdcp->hdcpss_mutex);
			}

			device_count = hdcp->bstatus[0] & DEVICE_COUNT_MASK;
		} else {
			/* DP Case */
			/* Poll for KSV List Ready every 1 second */
			for (i = 0; i < 12; i++) {
				ret = hdcpss_read_bstatus_dp(hdcp, info->display_id);
				if (!ret) {
					dev_err(hdcp->adev->dev,
						"Error in reading bstatus_dp\n");
					return ret;
				}
				if (hdcp->bstatus_dp & (1 << 0)) {
					dev_dbg(hdcp->adev->dev,
						"KSV FIFO Ready i = %x\n", i);
					break;
				}
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(msecs_to_jiffies(500));
			}

			if (!(hdcp->bstatus_dp & (1 << 0))) {
				dev_err(hdcp->adev->dev,
					"KSV list not ready after 5 seconds\n");
				if (hdcp->reauth_timeout) {
					hdcp->reauth_timeout = 0;
					mutex_unlock(&hdcp->hdcpss_mutex);
					hdcpss_notify_hotplug_detect(hdcp->adev,
							0,
							info->display_id);
					hdcpss_notify_hotplug_detect(hdcp->adev,
							1,
							info->display_id);
					mutex_lock(&hdcp->hdcpss_mutex);
				} else {
					dev_err(hdcp->adev->dev,
					"Reauthentication attempt failed\n");
					hdcp->ksv_timeout_err = 1;
					hdcp->reauth_timeout = 1;
					return -ETIMEDOUT;
				}
			}

			if (hdcp->ksv_timeout_err) {
				dev_err(hdcp->adev->dev, "Reattempt failed \n");
				ret = hdcpss_send_close_session(hdcp, info);
				if (ret) {
					dev_err(hdcp->adev->dev,
					"error in close session\n");
					return ret;
				}
				/*
				 * Setting flag to indicate session
				 * closed for particular dig_id
				 */
				hdcp->is_session_closed[info->link_enc_hw_inst] = 1;

				hdcp->ksv_timeout_err = 0;
				return ret;
			}

			/* Read Binfo to determine the count */
			ret = hdcpss_read_Binfo(hdcp, info->display_id);
			if (!ret) {
				dev_err(hdcp->adev->dev,
						"Error in reading Binfo\n");
				return ret;
			}

			if (hdcp->Binfo[0] & (1 << BIT_MAX_DEVS_EXCEDDED)) {
				dev_err(hdcp->adev->dev,
					"Topology error: MAX_DEVS_EXCEDDED\n");
				mutex_unlock(&hdcp->hdcpss_mutex);
				hdcpss_notify_hotplug_detect(hdcp->adev,
							0,
							info->display_id);
				hdcpss_notify_hotplug_detect(hdcp->adev,
							1,
							info->display_id);
				mutex_lock(&hdcp->hdcpss_mutex);
			}

			if (hdcp->Binfo[1] &
					(1 << BIT_MAX_CASCADE_EXCEDDED)) {
				dev_err(hdcp->adev->dev,
					"Topology error: MAX_CASCADE_EXCEDDED\n");
				mutex_unlock(&hdcp->hdcpss_mutex);
				hdcpss_notify_hotplug_detect(hdcp->adev,
							0,
							info->display_id);
				hdcpss_notify_hotplug_detect(hdcp->adev,
							1,
							info->display_id);
				mutex_lock(&hdcp->hdcpss_mutex);
                        }

			device_count = hdcp->Binfo[0] & DEVICE_COUNT_MASK;
		}

		dev_info(hdcp->adev->dev, "Device count = %d\n", device_count);

		if (device_count > 0) {
			/* Read the KSV FIFO */
			ret = hdcpss_read_ksv_fifo(hdcp, info->display_id,
								device_count);
			if (!ret) {
				dev_err(hdcp->adev->dev,
						"Error in reading KSV fifo\n");
				return ret;
			}
		}

		/* Read Pj' */
		if (hdcp->connector_type == HDCP_14_CONNECTOR_TYPE_HDMI) {
			ret = hdcpss_read_Pj(hdcp, info->display_id);
			if (!ret) {
				dev_err(hdcp->adev->dev,
						"Error in reading Pj\n");
				return ret;
			}
		}
		for(i = 0; i < MAX_V_PRIME_READ_RETRIES; i++) {
			/* Read V' */
			ret = hdcpss_read_v_prime(hdcp, info->display_id);
			if (!ret) {
				dev_err(hdcp->adev->dev,
						"Error in reading V'\n");
				return ret;
			}

			ret = hdcpss_send_second_part_auth(hdcp, info,
							HDCP_LINK_PRIMARY);
			if (!ret) {
				dev_info(hdcp->adev->dev,
					"Second Part Authentication success\n");
				break;
			} else {
				dev_err(hdcp->adev->dev,
					"Second Part Authentication failed\n");
				if (hdcp->reauth_V) {
					if (i == (MAX_V_PRIME_READ_RETRIES - 1)) {
						hdcp->reauth_V = 0;
						mutex_unlock(&hdcp->hdcpss_mutex);
						hdcpss_notify_hotplug_detect(
							hdcp->adev,
							0,
							info->display_id);
						hdcpss_notify_hotplug_detect(
							hdcp->adev,
							1,
							info->display_id);
						mutex_lock(&hdcp->hdcpss_mutex);
					}
				} else {
					dev_err(hdcp->adev->dev,
						"Reattempt auth failed\n");
					ret = hdcpss_send_close_session(hdcp, info);
					if (ret) {
						dev_err(hdcp->adev->dev,
						"error in close session\n");
						return ret;
					}
					/*
					 * Setting flag to indicate session
					 * closed for particular dig_id
					 */
				        hdcp->is_session_closed[info->link_enc_hw_inst] = 1;

					/* Restore back the attempt flag */
					hdcp->reauth_V = 1;
					return ret;
				}
			}
		}
	}

	return 0;
}

int hdcpss_send_close_session(struct hdcpss_data *hdcp, struct dm_hdcp_info *info)
{
	int ret = 0;

	if (hdcp->session_id == 0)
		return 1;

	memset(hdcp->tci_buf_addr, 0, sizeof(HDCP_TCI));


	if (hdcp->is_session_closed[info->link_enc_hw_inst]) {
		dev_info(hdcp->adev->dev,
			"Session already closed for dig_ID = %x\n",
			info->link_enc_hw_inst);
		return 0;
	}

	hdcp->tci_buf_addr->HDCP_14_Message.CommandHeader.
		commandId = HDCP_CMD_HOST_CMDS;
	hdcp->tci_buf_addr->eHDCPSessionType = HDCP_14;
	hdcp->tci_buf_addr->eHDCPCommand = TL_HDCP_CMD_ID_CLOSE_SESSION;
	hdcp->tci_buf_addr->HDCP_14_Message.CmdHDCPCmdInput.DigId = info->link_enc_hw_inst;

	dev_info(hdcp->adev->dev,
		"Sending command TL_HDCP_CMD_ID_CLOSE_SESSION for digID = %x\n",
				hdcp->tci_buf_addr->HDCP_14_Message.
				CmdHDCPCmdInput.DigId);

	ret = hdcpss_notify_ta(hdcp);

	if (1 != hdcp->tci_buf_addr->HDCP_14_Message.ResponseHeader.
					returnCode) {
		dev_err(hdcp->adev->dev, "Error from Trustlet = %d\n",
				hdcp->tci_buf_addr->HDCP_14_Message.
				ResponseHeader.returnCode);
		ret = 1;
	}

	dev_info(hdcp->adev->dev, "TA return code = %d\n",
			hdcp->tci_buf_addr->HDCP_14_Message.
			ResponseHeader.returnCode);
	dev_info(hdcp->adev->dev, "TA response code = %d\n",
			hdcp->tci_buf_addr->HDCP_14_Message.
			RspHDCPCmdOutput.bResponseCode);
	return 0;
}

/*
 *  This function will be called by DAL when it detects cable plug/unplug event
 *  void *display_id : - Display identifier
 *  int event :- event = 1 (Connect),
 *               event = 0 (Disconnect)
 */

void hdcpss_notify_hotplug_detect(struct amdgpu_device *adev, int event, void *display_id)
{
	struct hdcpss_data *hdcp = &adev->hdcp;
	struct dm_hdcp_info info;

	if (!amdgpu_dm_get_hdcp_info(display_id, &info))
		return;

	mutex_lock(&hdcp->hdcpss_mutex);

	if (event) {
		printk("Connect event detected display_index = %p\n", display_id);

		hdcpss_start_hdcp14_authentication(hdcp, &info);
	} else {
		printk("Disconnect event detected display_index = %p\n", display_id);
		hdcpss_send_close_session(hdcp, &info);
		hdcpss_get_encryption_level(hdcp, display_id, NULL);
	}
	mutex_unlock(&hdcp->hdcpss_mutex);
}
EXPORT_SYMBOL(hdcpss_notify_hotplug_detect);

void hdcpss_handle_cpirq(struct amdgpu_device *adev, void *display_id)
{
	bool    ret;
	struct hdcpss_data *hdcp = &adev->hdcp;
	struct dm_hdcp_info info;

	if (!amdgpu_dm_get_hdcp_info(display_id, &info))
		return;

	/* Read DP BStatus */
	ret = hdcpss_read_bstatus_dp(hdcp, display_id);
	if (!ret) {
		dev_err(hdcp->adev->dev,
				"Error in reading bstatus_dp\n");
		return;
	}

	if (hdcp->bstatus_dp & (1 << BIT_DP_LINK_INTEGRITY_FAILURE)) {
		dev_err(hdcp->adev->dev,
				"Link Integrity failure\n");
		hdcpss_notify_hotplug_detect(hdcp->adev,
						0,
						display_id);
		hdcpss_notify_hotplug_detect(hdcp->adev,
						1,
						display_id);
	}

	if (hdcp->bstatus_dp & (1 << BIT_DP_REAUTH_REQUEST)) {
		dev_err(hdcp->adev->dev,
				"Reauthentication request\n");
		hdcpss_notify_hotplug_detect(hdcp->adev,
						0,
						display_id);
		hdcpss_notify_hotplug_detect(hdcp->adev,
						1,
						display_id);
	}

}
EXPORT_SYMBOL(hdcpss_handle_cpirq);

static inline void *getpagestart(void *addr)
{
	return (void *)(((u64)(addr)) & PAGE_MASK);
}

static inline u32 getoffsetinpage(void *addr)
{
	return (u32)(((u64)(addr)) & (~PAGE_MASK));
}

static inline u32 getnrofpagesforbuffer(void *addrStart, u32 len)
{
	return (getoffsetinpage(addrStart) + len + PAGE_SIZE-1) / PAGE_SIZE;
}

static void flush_buffer(void *addr, u32 size)
{
	struct page *page;
	void *page_start = getpagestart(addr);
	int i;

	for (i = 0; i < getnrofpagesforbuffer(addr, size); i++) {
		page = virt_to_page(page_start);
		flush_dcache_page(page);
		page_start += PAGE_SIZE;
	}
}

int hdcpss_load_ta(struct hdcpss_data *hdcp)
{
	int ret = 0;
	int fence_val = 0;

	/* Create the LOAD_TA command */
	hdcp->cmd_buf_addr->buf_size	= sizeof(struct gfx_cmd_resp);
	hdcp->cmd_buf_addr->buf_version	= PSP_GFX_CMD_BUF_VERSION;
	hdcp->cmd_buf_addr->cmd_id	= GFX_CMD_ID_LOAD_TA;

	hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_hi =
			upper_32_bits(virt_to_phys(hdcp->ta_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_lo =
			lower_32_bits(virt_to_phys(hdcp->ta_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.app_len = hdcp->ta_size;

	hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_hi =
			upper_32_bits(virt_to_phys(hdcp->tci_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_lo =
			lower_32_bits(virt_to_phys(hdcp->tci_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.tci_buf_len = hdcp->tci_size;

	/* No WSM buffer */
	hdcp->cmd_buf_addr->u.load_ta.wsm_buf_phy_addr_hi = 0;
	hdcp->cmd_buf_addr->u.load_ta.wsm_buf_phy_addr_lo = 0;
	hdcp->cmd_buf_addr->u.load_ta.wsm_len = 0;

	dev_dbg(hdcp->adev->dev, "Dumping Load TA command contents\n");
	dev_dbg(hdcp->adev->dev, "Buf size = %d\n",
					hdcp->cmd_buf_addr->buf_size);
	dev_dbg(hdcp->adev->dev, "Buf verion = %x\n",
					hdcp->cmd_buf_addr->buf_version);
	dev_dbg(hdcp->adev->dev, "Command ID = %x\n",
					hdcp->cmd_buf_addr->cmd_id);
	dev_dbg(hdcp->adev->dev, "TA phy addr hi = %x\n",
			hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_hi);
	dev_dbg(hdcp->adev->dev, "TA phy addr lo = %x\n",
			hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_lo);
	dev_dbg(hdcp->adev->dev, "TA Size = %d\n",
				hdcp->cmd_buf_addr->u.load_ta.app_len);
	dev_dbg(hdcp->adev->dev, "TCI Phy addr hi  = %x\n",
		hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_hi);
	dev_dbg(hdcp->adev->dev, "TCI Phy addr lo = %x\n",
		hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_lo);
	dev_dbg(hdcp->adev->dev, "TCI Size = %d\n",
			hdcp->cmd_buf_addr->u.load_ta.tci_buf_len);

	/* Flush TA buffer */
	flush_buffer((void *)hdcp->ta_buf_addr, hdcp->ta_size);

	/* Flush TCI buffer */
	flush_buffer((HDCP_TCI *)hdcp->tci_buf_addr, hdcp->tci_size);

	/* Initialize fence value */
	fence_val = 0x11111111;

	ret = g2p_comm_send_command_buffer(hdcp->cmd_buf_addr,
					sizeof(struct gfx_cmd_resp),
					fence_val);
	if (ret) {
		dev_err(hdcp->adev->dev, "LOAD TA failed\n");
		dev_err(hdcp->adev->dev, "status = %d\n",
					hdcp->cmd_buf_addr->resp.status);
		free_pages((unsigned long)hdcp->tci_buf_addr,
						get_order(hdcp->tci_size));
		free_pages((unsigned long)hdcp->ta_buf_addr,
						get_order(hdcp->ta_size));
		free_pages((unsigned long)hdcp->cmd_buf_addr,
					get_order(hdcp->cmd_buf_size));
		return ret;
	}

	dev_dbg(hdcp->adev->dev, "status = %d\n",
					hdcp->cmd_buf_addr->resp.status);

	/* Check for response from PSP in Response buffer */
	if (!hdcp->cmd_buf_addr->resp.status) {
		dev_info(hdcp->adev->dev, "session_id = %d\n",
				hdcp->cmd_buf_addr->resp.session_id);
	} else {
		dev_err(hdcp->adev->dev, "Load TA failed, PSP resp = %x\n",
				hdcp->cmd_buf_addr->resp.status);
		return ret;
	}

	hdcp->session_id = hdcp->cmd_buf_addr->resp.session_id;

	dev_info(hdcp->adev->dev, "Loaded Trusted Application successfully\n");

	return ret;
}

int hdcpss_unload_ta(struct hdcpss_data *hdcp)
{
	int ret = 0;
	int fence_val = 0;

	/* Submit UNLOAD_TA command */
	hdcp->cmd_buf_addr->buf_size	= sizeof(struct gfx_cmd_resp);
	hdcp->cmd_buf_addr->buf_version	= PSP_GFX_CMD_BUF_VERSION;
	hdcp->cmd_buf_addr->cmd_id	= GFX_CMD_ID_UNLOAD_TA;

	hdcp->cmd_buf_addr->u.unload_ta.session_id = hdcp->session_id;

	dev_dbg(hdcp->adev->dev, "Command id = %d\n",
					hdcp->cmd_buf_addr->cmd_id);

	fence_val = 0x33333333;

	ret = g2p_comm_send_command_buffer(hdcp->cmd_buf_addr,
					sizeof(struct gfx_cmd_resp),
					fence_val);
	if (ret) {
		dev_err(hdcp->adev->dev, "Unloading TA failed\n");
		dev_err(hdcp->adev->dev, "status = %d\n",
					hdcp->cmd_buf_addr->resp.status);
		free_pages((unsigned long)hdcp->tci_buf_addr,
				get_order(hdcp->tci_size));
		free_pages((unsigned long)hdcp->ta_buf_addr,
				get_order(hdcp->ta_size));
		free_pages((unsigned long)hdcp->cmd_buf_addr,
				get_order(hdcp->cmd_buf_size));
		return ret;
	}

	dev_info(hdcp->adev->dev,
			"Unloaded Trusted Application successfully\n");

	return ret;
}

int hdcpss_notify_ta(struct hdcpss_data *hdcp)
{
	int ret = 0;
	int fence_val = 0;

	dev_dbg(hdcp->adev->dev, "session_id = %d\n", hdcp->session_id);

	/* Submit NOTIFY_TA command */
	hdcp->cmd_buf_addr->buf_size	= sizeof(struct gfx_cmd_resp);
	hdcp->cmd_buf_addr->buf_version	= PSP_GFX_CMD_BUF_VERSION;
	hdcp->cmd_buf_addr->cmd_id	= GFX_CMD_ID_NOTIFY_TA;
	hdcp->cmd_buf_addr->u.notify_ta.session_id = hdcp->session_id;

	/* Flush TCI buffer */
	flush_buffer((HDCP_TCI *)hdcp->tci_buf_addr, hdcp->tci_size);

	fence_val = 0x22222222;

	ret = g2p_comm_send_command_buffer(hdcp->cmd_buf_addr,
					sizeof(struct gfx_cmd_resp),
					fence_val);
	if (ret) {
		dev_err(hdcp->adev->dev, "NOTIFY TA failed\n");
		dev_err(hdcp->adev->dev, "status = %d\n",
					hdcp->cmd_buf_addr->resp.status);
		free_pages((unsigned long)hdcp->tci_buf_addr,
						get_order(hdcp->tci_size));
		free_pages((unsigned long)hdcp->ta_buf_addr,
						get_order(hdcp->ta_size));
		free_pages((unsigned long)hdcp->cmd_buf_addr,
					get_order(hdcp->cmd_buf_size));
		return ret;
	}

	if (!hdcp->cmd_buf_addr->resp.status)
		dev_dbg(hdcp->adev->dev, "NOTIFY TA success status =  %x\n",
					hdcp->cmd_buf_addr->resp.status);
	return ret;
}

int hdcpss_load_asd(struct hdcpss_data *hdcp)
{
	int ret = 0;
	int fence_val = 0;

	/* Create the LOAD_ASD command */
	hdcp->cmd_buf_addr->buf_size	= sizeof(struct gfx_cmd_resp);
	hdcp->cmd_buf_addr->buf_version	= PSP_GFX_CMD_BUF_VERSION;
	hdcp->cmd_buf_addr->cmd_id	= GFX_CMD_ID_LOAD_ASD;

	hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_hi =
			upper_32_bits(virt_to_phys(hdcp->asd_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_lo =
			lower_32_bits(virt_to_phys(hdcp->asd_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.app_len = hdcp->asd_size;

	hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_hi =
			upper_32_bits(virt_to_phys(hdcp->dci_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_lo =
			lower_32_bits(virt_to_phys(hdcp->dci_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.tci_buf_len = hdcp->dci_size;

	/* No WSM buffer */
	hdcp->cmd_buf_addr->u.load_ta.wsm_buf_phy_addr_hi = 0;
	hdcp->cmd_buf_addr->u.load_ta.wsm_buf_phy_addr_lo = 0;
	hdcp->cmd_buf_addr->u.load_ta.wsm_len = 0;

	dev_dbg(hdcp->adev->dev, "Dumping Load ASD command contents\n");
	dev_dbg(hdcp->adev->dev, "Buf size = %d\n",
					hdcp->cmd_buf_addr->buf_size);
	dev_dbg(hdcp->adev->dev, "Buf verion = %x\n",
					hdcp->cmd_buf_addr->buf_version);
	dev_dbg(hdcp->adev->dev, "Command ID = %x\n",
					hdcp->cmd_buf_addr->cmd_id);
	dev_dbg(hdcp->adev->dev, "TA phy addr hi = %x\n",
			hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_hi);
	dev_dbg(hdcp->adev->dev, "TA phy addr lo = %x\n",
			hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_lo);
	dev_dbg(hdcp->adev->dev, "TA Size = %d\n",
				hdcp->cmd_buf_addr->u.load_ta.app_len);
	dev_dbg(hdcp->adev->dev, "DCI Phy addr hi  = %x\n",
		hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_hi);
	dev_dbg(hdcp->adev->dev, "DCI Phy addr lo = %x\n",
		hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_lo);
	dev_dbg(hdcp->adev->dev, "DCI Size = %d\n",
			hdcp->cmd_buf_addr->u.load_ta.tci_buf_len);

	/* Flush ASD buffer */
	flush_buffer((void *)hdcp->asd_buf_addr, hdcp->asd_size);

	/* Flush DCI buffer */
	flush_buffer((HDCP_TCI *)hdcp->dci_buf_addr, hdcp->dci_size);

	/* Initialize fence value */
	fence_val = 0x44444444;

	ret = g2p_comm_send_command_buffer(hdcp->cmd_buf_addr,
					sizeof(struct gfx_cmd_resp),
					fence_val);
	if (ret) {
		dev_err(hdcp->adev->dev, "LOAD ASD failed\n");
		dev_err(hdcp->adev->dev, "status = %d\n",
					hdcp->cmd_buf_addr->resp.status);
		free_pages((unsigned long)hdcp->dci_buf_addr,
						get_order(hdcp->dci_size));
		free_pages((unsigned long)hdcp->asd_buf_addr,
						get_order(hdcp->asd_size));
		free_pages((unsigned long)hdcp->cmd_buf_addr,
					get_order(hdcp->cmd_buf_size));
		return ret;
	}

	dev_dbg(hdcp->adev->dev, "status = %d\n",
					hdcp->cmd_buf_addr->resp.status);

	/* Check for response from PSP in Response buffer */
	if (!hdcp->cmd_buf_addr->resp.status) {
		dev_info(hdcp->adev->dev, "ASD session_id = %d\n",
				hdcp->cmd_buf_addr->resp.session_id);
	} else {
		dev_err(hdcp->adev->dev, "Load ASD failed PSP resp = %x\n",
				hdcp->cmd_buf_addr->resp.status);
		return ret;
	}

	hdcp->asd_session_id = hdcp->cmd_buf_addr->resp.session_id;

	dev_info(hdcp->adev->dev, "Loaded ASD driver successfully\n");

	return ret;
}

/*
 *  This function needs to be called as part of init sequence probably
 *  from the display driver.
 *  This function is responsible to allocate required memory for
 *  command buffers and TCI.
 *  This function will require to load the HDCP Trusted application
 *  in the PSP secure world using LOAD_TA command.
 *
 */
int hdcpss_init(void *handle)
{
	int ret = 0;
	const char *fw_name = FIRMWARE_CARRIZO;
	const char *asd_bin_name = ASD_BIN_CARRIZO;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct hdcpss_data *hdcp;

	hdcp = &adev->hdcp;
	hdcp->adev  = adev;

	dev_dbg(adev->dev, "%s\n", __func__);

	hdcp->cmd_buf_size = sizeof(struct gfx_cmd_resp);

	/* Allocate physically contiguous memory for the command buffer */
	hdcp->cmd_buf_addr = (struct gfx_cmd_resp *)
					__get_free_pages(GFP_KERNEL,
					get_order(hdcp->cmd_buf_size));
	if (!hdcp->cmd_buf_addr) {
		dev_err(adev->dev, "command buffer memory allocation failed\n");
		return -ENOMEM;
	}

	dev_dbg(adev->dev, "Command buffer address = %p\n", hdcp->cmd_buf_addr);

	/* Request ASD firmware */
	ret = request_firmware(&adev->psp.fw, asd_bin_name, adev->dev);
	if (ret) {
		dev_err(adev->dev, "amdgpu_psp: Can't load firmware \"%s\"\n",
							asd_bin_name);
		return ret;
	}

	dev_info(adev->dev, "request_fw success: size of asd_bin= %ld\n",
							adev->psp.fw->size);

	hdcp->asd_size = adev->psp.fw->size;

	/* Allocate physically contiguos memory to hold the ASD binary */
	hdcp->asd_buf_addr = (u64 *)__get_free_pages(GFP_KERNEL,
						get_order(hdcp->asd_size));
	if (!hdcp->asd_buf_addr) {
		dev_err(adev->dev, "ASD buffer memory allocation failed\n");
		free_pages((unsigned long)hdcp->cmd_buf_addr,
					get_order(hdcp->cmd_buf_size));
		return -ENOMEM;
	}
	dev_dbg(adev->dev, "ASD buffer address = %p\n", hdcp->asd_buf_addr);

	/* Copy the ASD binary into the allocated buffer */
	memcpy(hdcp->asd_buf_addr, adev->psp.fw->data, hdcp->asd_size);

	release_firmware(adev->psp.fw);

	/* Request TA binary */
	ret = request_firmware(&adev->psp.fw, fw_name, adev->dev);
	if (ret) {
		dev_err(adev->dev, "amdgpu_psp: Can't load firmware \"%s\"\n",
						fw_name);
		return ret;
	}

	dev_info(adev->dev, "request_fw success: size of ta = %ld\n",
							adev->psp.fw->size);

	hdcp->ta_size = adev->psp.fw->size;

	/* Allocate physically contiguos memory to hold the TA binary */
	hdcp->ta_buf_addr = (u64 *)__get_free_pages(GFP_KERNEL,
						get_order(hdcp->ta_size));
	if (!hdcp->ta_buf_addr) {
		dev_err(adev->dev, "TA buffer memory allocation failed\n");
		free_pages((unsigned long)hdcp->cmd_buf_addr,
					get_order(hdcp->cmd_buf_size));
		return -ENOMEM;
	}
	dev_dbg(adev->dev, "TA buffer address = %p\n", hdcp->ta_buf_addr);

	/* Copy the TA binary into the allocated buffer */
	memcpy(hdcp->ta_buf_addr, adev->psp.fw->data, hdcp->ta_size);

	release_firmware(adev->psp.fw);

	/*Allocate physically contigous memory for DCI */

	hdcp->dci_size = sizeof(dciMessage_t);
	hdcp->dci_buf_addr = (dciMessage_t *)__get_free_pages(GFP_KERNEL,
						get_order(hdcp->dci_size));
	if (!hdcp->dci_buf_addr) {
		dev_err(adev->dev, "TCI memory allocation failure\n");
		free_pages((unsigned long)hdcp->asd_buf_addr,
						get_order(hdcp->asd_size));
		free_pages((unsigned long)hdcp->cmd_buf_addr,
					get_order(hdcp->cmd_buf_size));
		return -ENOMEM;
	}
	dev_dbg(adev->dev, "DCI buffer address = %p\n", hdcp->dci_buf_addr);

	/* Load ASD Firmware */
	ret = hdcpss_load_asd(hdcp);

	/* Allocate physically contiguos memory for TCI */
	hdcp->tci_size = sizeof(HDCP_TCI);
	hdcp->tci_buf_addr = (HDCP_TCI *)__get_free_pages(GFP_KERNEL,
						get_order(hdcp->tci_size));
	if (!hdcp->tci_buf_addr) {
		dev_err(adev->dev, "TCI memory allocation failure\n");
		free_pages((unsigned long)hdcp->ta_buf_addr,
						get_order(hdcp->ta_size));
		free_pages((unsigned long)hdcp->cmd_buf_addr,
					get_order(hdcp->cmd_buf_size));
		return -ENOMEM;
	}
	dev_dbg(adev->dev, "TCI buffer address = %p\n", hdcp->tci_buf_addr);

	/* Load TA Binary */
	ret = hdcpss_load_ta(hdcp);

	mutex_init(&hdcp->hdcpss_mutex);

	hdcp->reauth_r0 = 1;
	hdcp->reauth_V = 1;
	hdcp->reauth_timeout = 1;
	hdcp->ksv_timeout_err = 0;

	dev_dbg(adev->dev, "%s exit\n", __func__);
	return 0;
}
EXPORT_SYMBOL(hdcpss_init);
