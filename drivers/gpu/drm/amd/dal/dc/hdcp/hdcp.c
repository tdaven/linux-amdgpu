/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "dm_services.h"
#include "dm_helpers.h"
#include "include/hdcp_types.h"
#include "include/i2caux_interface.h"
#include "include/signal_types.h"
#include "adapter/adapter_service.h"
#include "core_types.h"
#include "dc_link_ddc.h"
#include "link_hwss.h"

static const bool hdcp_cmd_is_read[] = {
	[HDCP_MESSAGE_ID_READ_BKSV] = true,
	[HDCP_MESSAGE_ID_READ_RI_R0] = true,
	[HDCP_MESSAGE_ID_READ_PJ] = true,
	[HDCP_MESSAGE_ID_WRITE_AKSV] = false,
	[HDCP_MESSAGE_ID_WRITE_AINFO] = false,
	[HDCP_MESSAGE_ID_WRITE_AN] = false,
	[HDCP_MESSAGE_ID_READ_VH_X] = true,
	[HDCP_MESSAGE_ID_READ_BCAPS] = true,
	[HDCP_MESSAGE_ID_READ_BSTATUS] = true,
	[HDCP_MESSAGE_ID_READ_KSV_FIFO] = true,
	[HDCP_MESSAGE_ID_READ_BINFO] = true,
	[HDCP_MESSAGE_ID_HDCP2VERSION] = true,
	[HDCP_MESSAGE_ID_WRITE_AKE_INIT] = false,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_CERT] = true,
	[HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM] = false,
	[HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM] = false,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME] = true,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO] = true,
	[HDCP_MESSAGE_ID_WRITE_LC_INIT] = false,
	[HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME] = true,
	[HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS] = false,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST] = true,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK] = false,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE] = false,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY] = true,
	[HDCP_MESSAGE_ID_READ_RXSTATUS] = true,
	[HDCP_MESSAGE_ID_READ_EC_RSP] = true,
	[HDCP_MESSAGE_ID_WRITE_EC_CMD] = false
};

static const uint8_t hdcp_i2c_offsets[] = {
	[HDCP_MESSAGE_ID_READ_BKSV] = 0x0,
	[HDCP_MESSAGE_ID_READ_RI_R0] = 0x8,
	[HDCP_MESSAGE_ID_READ_PJ] = 0xA,
	[HDCP_MESSAGE_ID_WRITE_AKSV] = 0x10,
	[HDCP_MESSAGE_ID_WRITE_AINFO] = 0x15,
	[HDCP_MESSAGE_ID_WRITE_AN] = 0x18,
	[HDCP_MESSAGE_ID_READ_VH_X] = 0x20,
	[HDCP_MESSAGE_ID_READ_BCAPS] = 0x40,
	[HDCP_MESSAGE_ID_READ_BSTATUS] = 0x41,
	[HDCP_MESSAGE_ID_READ_KSV_FIFO] = 0x43,
	[HDCP_MESSAGE_ID_READ_BINFO] = 0xFF,
	[HDCP_MESSAGE_ID_HDCP2VERSION] = 0x50,
	[HDCP_MESSAGE_ID_WRITE_AKE_INIT] = 0x60,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_CERT] = 0x80,
	[HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM] = 0x60,
	[HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM] = 0x60,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME] = 0x80,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO] = 0x80,
	[HDCP_MESSAGE_ID_WRITE_LC_INIT] = 0x60,
	[HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME] = 0x80,
	[HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS] = 0x60,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST] = 0x80,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK] = 0x60,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE] = 0x60,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY] = 0x80,
	[HDCP_MESSAGE_ID_READ_RXSTATUS] = 0x70
};

struct protection_properties {
	bool supported;
	bool (*process_transaction)(
		struct core_link *link,
		struct hdcp_protection_message *message_info);
};

static const struct protection_properties non_supported_protection = {
	.supported = false
};

static bool hdmi_14_process_transaction(
	struct core_link *link,
	struct hdcp_protection_message *message_info)
{
	uint8_t *buff = NULL;
	bool result;
	const uint8_t hdcp_i2c_addr_link_primary = 0x3a; /* 0x74 >> 1*/
	const uint8_t hdcp_i2c_addr_link_secondary = 0x3b; /* 0x76 >> 1*/
	struct i2c_command i2c_command;
	uint8_t offset = hdcp_i2c_offsets[message_info->msg_id];
	struct i2c_payload i2c_payloads[] = {
		{ true, 0, 1, &offset },
		/* actual hdcp payload, will be filled later, zeroed for now*/
		{ 0 }
	};

	switch (message_info->link) {
	case HDCP_LINK_SECONDARY:
		i2c_payloads[0].address = hdcp_i2c_addr_link_secondary;
		i2c_payloads[1].address = hdcp_i2c_addr_link_secondary;
		break;
	case HDCP_LINK_PRIMARY:
	default:
		i2c_payloads[0].address = hdcp_i2c_addr_link_primary;
		i2c_payloads[1].address = hdcp_i2c_addr_link_primary;
		break;
	}

	if (hdcp_cmd_is_read[message_info->msg_id]) {
		i2c_payloads[1].write = false;
		i2c_command.number_of_payloads = ARRAY_SIZE(i2c_payloads);
		i2c_payloads[1].length = message_info->length;
		i2c_payloads[1].data = message_info->data;
	} else {
		i2c_command.number_of_payloads = 1;
		buff = dm_alloc(message_info->length + 1);

		if (!buff)
			return false;

		buff[0] = offset;
		memmove(&buff[1], message_info->data, message_info->length);
		i2c_payloads[0].length = message_info->length + 1;
		i2c_payloads[0].data = buff;
	}

	i2c_command.payloads = i2c_payloads;
	i2c_command.engine = I2C_COMMAND_ENGINE_SW;
	i2c_command.speed = dal_adapter_service_get_sw_i2c_speed(link->adapter_srv);

	result = dm_helpers_submit_i2c(
			link->ctx,
			&link->public,
			&i2c_command);

	if (buff)
		dm_free(buff);

	return result;
}

static const struct protection_properties hdmi_14_protection = {
	.supported = true,
	.process_transaction = hdmi_14_process_transaction
};

static const uint32_t hdcp_dpcd_addrs[] = {
	[HDCP_MESSAGE_ID_READ_BKSV] = 0x68000,
	[HDCP_MESSAGE_ID_READ_RI_R0] = 0x68005,
	[HDCP_MESSAGE_ID_READ_PJ] = 0xFFFFFFFF,
	[HDCP_MESSAGE_ID_WRITE_AKSV] = 0x68007,
	[HDCP_MESSAGE_ID_WRITE_AINFO] = 0x6803B,
	[HDCP_MESSAGE_ID_WRITE_AN] = 0x6800c,
	[HDCP_MESSAGE_ID_READ_VH_X] = 0x68014,
	[HDCP_MESSAGE_ID_READ_BCAPS] = 0x68028,
	[HDCP_MESSAGE_ID_READ_BSTATUS] = 0x68029,
	[HDCP_MESSAGE_ID_READ_KSV_FIFO] = 0x6802c,
	[HDCP_MESSAGE_ID_READ_BINFO] = 0x6802a,
	[HDCP_MESSAGE_ID_WRITE_AKE_INIT] = 0x69000,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_CERT] = 0x6900b,
	[HDCP_MESSAGE_ID_WRITE_AKE_NO_STORED_KM] = 0x69220,
	[HDCP_MESSAGE_ID_WRITE_AKE_STORED_KM] = 0x692e0,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_H_PRIME] = 0x692c0,
	[HDCP_MESSAGE_ID_READ_AKE_SEND_PAIRING_INFO] = 0x692e0,
	[HDCP_MESSAGE_ID_WRITE_LC_INIT] = 0x692f0,
	[HDCP_MESSAGE_ID_READ_LC_SEND_L_PRIME] = 0x692f8,
	[HDCP_MESSAGE_ID_WRITE_SKE_SEND_EKS] = 0x69318,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_SEND_RECEIVERID_LIST] = 0x69330,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_SEND_ACK] = 0x693e0,
	[HDCP_MESSAGE_ID_WRITE_REPEATER_AUTH_STREAM_MANAGE] = 0x693f0,
	[HDCP_MESSAGE_ID_READ_REPEATER_AUTH_STREAM_READY] = 0x69473,
	[HDCP_MESSAGE_ID_READ_RXSTATUS] = 0x69493
};

static bool dpcd_access_helper(
	struct core_link *link,
	uint32_t length,
	uint8_t *data,
	uint32_t dpcd_addr,
	bool is_read)
{
	enum dc_status status;
	uint32_t cur_length = 0;
	uint32_t offset = 0;

	while (length > 0) {
		if (length > DEFAULT_AUX_MAX_DATA_SIZE)
			cur_length = DEFAULT_AUX_MAX_DATA_SIZE;
		else
			cur_length = length;

		if (is_read) {
			status = core_link_read_dpcd(
				link,
				dpcd_addr + offset,
				data + offset,
				cur_length);
		} else {
			status = core_link_write_dpcd(
				link,
				dpcd_addr + offset,
				data + offset,
				cur_length);
		}

		if (status != DC_OK)
			return false;

		length -= cur_length;
		offset += cur_length;
	}

	return true;
}

static bool dp_11_process_transaction(
	struct core_link *link,
	struct hdcp_protection_message *message_info)
{
	return dpcd_access_helper(
		link,
		message_info->length,
		message_info->data,
		hdcp_dpcd_addrs[message_info->msg_id],
		hdcp_cmd_is_read[message_info->msg_id]);
}

static const struct protection_properties dp_11_protection = {
	.supported = true,
	.process_transaction = dp_11_process_transaction
};

enum ec_dpcd_addr {
	EC_DPCD_ADDR_CMD_STATUS = 0x70000,
	EC_DPCD_ADDR_RSP_STATUS = 0x70001,
	EC_DPCD_ADDR_CMD_BUFFER = 0x70010,
	EC_DPCD_ADDR_RSP_BUFFER = 0x70810
};

static bool ec_process_transaction(
	struct core_link *link,
	struct hdcp_protection_message *message_info)
{
	enum dc_status status;
	const int timeout_ms = 3000;
	const char sleep_time = 5;
	int max_tries = timeout_ms / sleep_time;
	int i;
	bool is_read = hdcp_cmd_is_read[message_info->msg_id];
	uint8_t buf;

	if (is_read) {
		for (i = 0; i < max_tries; ++i) {
			status = core_link_read_dpcd(
				link,
				EC_DPCD_ADDR_RSP_STATUS,
				&buf,
				sizeof(buf));

			if (buf == 1) /* ready */
				break;

			msleep(sleep_time);
		}

		if (i == max_tries)
			return false;

		if (!dpcd_access_helper(
			link,
			message_info->length,
			message_info->data,
			EC_DPCD_ADDR_RSP_BUFFER,
			is_read))
			return false;

		buf = 0; /* clear */

		status = core_link_write_dpcd(
			link,
			EC_DPCD_ADDR_RSP_STATUS,
			&buf,
			sizeof(buf));

		if (status != DC_OK)
			return false;
	} else {
		for (i = 0; i < max_tries; ++i) {
			status = core_link_read_dpcd(
				link,
				EC_DPCD_ADDR_CMD_STATUS,
				&buf,
				sizeof(buf));

			if (buf == 1) /* ready */
				break;

			msleep(sleep_time);
		}

		if (i == max_tries)
			return false;

		if (!dpcd_access_helper(
			link,
			message_info->length,
			message_info->data,
			EC_DPCD_ADDR_CMD_BUFFER,
			is_read))
			return false;

		buf = 0; /* clear */

		status = core_link_write_dpcd(
			link,
			EC_DPCD_ADDR_RSP_STATUS,
			&buf,
			sizeof(buf));

		if (status != DC_OK)
			return false;

		buf = 1; /* ready */

		status = core_link_write_dpcd(
			link,
			EC_DPCD_ADDR_CMD_STATUS,
			&buf,
			sizeof(buf));

		if (status != DC_OK)
			return false;
	}

	return true;
}

static const struct protection_properties ec_protection = {
	.supported = true,
	.process_transaction = ec_process_transaction
};

static const struct protection_properties *get_protection_properties_by_signal(
	enum signal_type st,
	enum hdcp_version version)
{
	switch (version) {
	case HDCP_VERSION_14:
	case HDCP_VERSION_22:
		switch (st) {
		case SIGNAL_TYPE_DVI_SINGLE_LINK:
		case SIGNAL_TYPE_DVI_DUAL_LINK:
		case SIGNAL_TYPE_HDMI_TYPE_A:
			return &hdmi_14_protection;
		case SIGNAL_TYPE_DISPLAY_PORT:
			return &dp_11_protection;
		default:
			return &non_supported_protection;
		}
		break;
	case HDCP_VERSION_22_EXTERNAL_CHIP:
		if (st == SIGNAL_TYPE_DISPLAY_PORT)
			return &ec_protection;
		else
			return &non_supported_protection;
	default:
		return &non_supported_protection;
	}
}

bool dc_process_hdcp_msg(
	const struct dc_stream *stream,
	struct hdcp_protection_message *message_info)
{

	struct core_sink *core_sink = DC_SINK_TO_CORE(stream->sink);
	bool result = false;

	const struct protection_properties *protection_props;

	if (!message_info)
		return false;

	if (message_info->msg_id < HDCP_MESSAGE_ID_READ_BKSV ||
		message_info->msg_id >= HDCP_MESSAGE_ID_MAX)
		return false;

	protection_props =
		get_protection_properties_by_signal(
			core_sink->public.sink_signal,
			message_info->version);

	if (!protection_props->supported)
		return false;

	result =
		protection_props->process_transaction(
			core_sink->link,
			message_info);

	return result;
}

