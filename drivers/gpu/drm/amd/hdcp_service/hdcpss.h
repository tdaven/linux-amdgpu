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

#ifndef __HDCPSS_H__
#define __HDCPSS_H__

#include <linux/types.h>
#include "tl_hdcp_if.h"
#include "drDrmApi.h"

#define PSP_GFX_CMD_BUF_VERSION	0x00000001
#define RET_OK			0

#define BIT_REPEATER			6
#define BIT_DP_REPEATER			1
#define BIT_FIFO_READY			5
#define BIT_DP_HDCP_CAPABLE		0
#define BIT_DP_LINK_INTEGRITY_FAILURE	2
#define BIT_DP_REAUTH_REQUEST		3

#define DEVICE_COUNT_MASK		0x7F
#define BIT_MAX_DEVS_EXCEDDED		7
#define BIT_MAX_CASCADE_EXCEDDED	3

#define MAX_R0_READ_RETRIES		3
#define MAX_V_PRIME_READ_RETRIES	3
#define MAX_DIG_ID			4

/* TEE Gfx Command IDs for the ring buffer interface. */
enum gfx_cmd_id {
	GFX_CMD_ID_LOAD_TA	= 0x00000001,	/* load TA		*/
	GFX_CMD_ID_UNLOAD_TA	= 0x00000002,	/* unload TA		*/
	GFX_CMD_ID_NOTIFY_TA	= 0x00000003,	/* notify TA		*/
	GFX_CMD_ID_LOAD_ASD	= 0x00000004,	/* load ASD Driver	*/
};

/* Command to load Trusted Application binary into PSP OS. */
struct gfx_cmd_load_ta {
	u32	app_phy_addr_hi;	/* bits[63:32] of TA physical addr   */
	u32	app_phy_addr_lo;	/* bits[31:0] of TA physical addr    */
	u32	app_len;		/* length of the TA binary	     */
	u32	tci_buf_phy_addr_hi;	/* bits[63:32] of TCI physical addr  */
	u32	tci_buf_phy_addr_lo;	/* bits[31:0] of TCI physical addr   */
	u32	tci_buf_len;		/* length of the TCI buffer in bytes */
	u32	wsm_buf_phy_addr_hi;	/* bits[63:32] of wsm physical addr  */
	u32	wsm_buf_phy_addr_lo;	/* bits[31:0] of wsm physical addr   */
	u32	wsm_len;		/* length of WSM buffer		     */
};

/* Command to Unload Trusted Application binary from PSP OS */
struct gfx_cmd_unload_ta {
	u32	session_id;	/* Session ID of the loaded TA */
};

/* Command to notify TA that it has to execute command in TCI buffer */
struct gfx_cmd_notify_ta {
	u32	session_id;	/* Session ID of the TA */
};

/*
 * Structure of GFX Response buffer.
 * For GPCOM I/F it is part of GFX_CMD_RESP buffer, for RBI
 * it is separate buffer.
 * Total size of GFX Response buffer = 32 bytes
 */
struct gfx_resp {
	u32	status;		/* status of command execution */
	u32	session_id;	/* session ID in response to LoadTa command */
	u32	wsm_virt_addr;	/* virtual address of shared buffer */
	u32	resv[5];
};

/*
 * Structure for Command buffer pointed by GFX_RB_FRAME.CmdBufAddrHi
 * and GFX_RB_FRAME.CmdBufAddrLo.
 * Total Size of gfx_cmd_resp = 128 bytes
 * union occupies 24 bytes.
 * resp buffer is of 32 bytes
 */
struct gfx_cmd_resp {
	u32	buf_size;		/* total size of the buffer in bytes */
	u32	buf_version;		/* version of the buffer structure */
	u32	cmd_id;			/* command ID */
	u32	resp_buf_addr_hi;	/* Used for RBI ring only */
	u32	resp_buf_addr_lo;	/* Used for RBI ring only */
	u32	resp_offset;		/* Used for RBI ring only */
	u32	resp_buf_size;		/* Used for RBI ring only */

	union {
		struct gfx_cmd_load_ta	 load_ta;
		struct gfx_cmd_unload_ta unload_ta;
		struct gfx_cmd_notify_ta notify_ta;
	} u;

	unsigned char   resv1[16];
	struct gfx_resp resp;		/* Response buffer for GPCOM ring */
	unsigned char   resv2[16];
};

struct hdcpss_data {
	struct mutex		hdcpss_mutex;
	u32			session_id;
	u32			asd_session_id;
	u32			cmd_buf_size;
	u32			ta_size;
	u32			asd_size;
	u32			tci_size;
	u32			dci_size;
	struct gfx_cmd_resp	*cmd_buf_addr;
	HDCP_TCI		*tci_buf_addr;
	dciMessage_t		*dci_buf_addr;
	void			*ta_buf_addr;
	void			*asd_buf_addr;
	uint8_t			AnPrimary[8];
	uint8_t			AksvPrimary[5];
	uint8_t			BksvPrimary[5];
	uint8_t			BksvSecondary[5];
	uint8_t			Bcaps;
	uint8_t			R_Prime[2];
	uint8_t			*ksv_fifo_buf;
	u32			ksv_list_size;
	uint8_t			V_Prime[20];
	uint8_t			bstatus[2];
	struct amdgpu_device	*adev;
	uint8_t			is_repeater;
	uint8_t			is_primary_link;
	uint8_t			dig_id;
	uint8_t			Ainfo;
	uint32_t		connector_type;
	uint8_t			Pj;
	uint8_t			Binfo[2];
	uint8_t			bstatus_dp;
	uint8_t			reauth_r0;
	uint8_t			reauth_V;
	uint8_t			reauth_timeout;
	uint8_t			ksv_timeout_err;
	uint8_t			is_session_closed[MAX_DIG_ID];
};

#endif
