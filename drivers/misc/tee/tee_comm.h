/*
* tee_comm.h - communication protocal with secure world
*
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

#ifndef _TEE_COMM_H_
#define _TEE_COMM_H_

#include "tee_driver.h"
#include "helper.h"
#include <linux/types.h>

#define PSP_TEE_API_VERSION		(0x00000001)
#define PSP_TEE_CMD_ID_MASK		(0x00FF0000)
#define WSM_BUF_MAX_LEN			(0x00100000)
#define TEE_API_VERSION			(0x00000001)

#define TEE_STATUS_ORIGIN_NWD		(1)
#define TEE_STATUS_ORIGIN_SWD		(2)

#define MAX_SESSIONS_SUPPORTED		(16)
#define MAX_BUFFERS_MAPPED		(6)
#define MAX_QUEUES_ENTRIES		(16)
#define TEE_PRODUCT_ID_LEN		(64)

/* tee command ids */
enum tee_cmd_id {
	TEE_CMD_ID_GET_VERSION_INFO		= 0x00010000,
	TEE_CMD_ID_OPEN_SESSION			= 0x00020000,
	TEE_CMD_ID_CLOSE_SESSION		= 0x00030000,
	TEE_CMD_ID_MAP				= 0x00040000,
	TEE_CMD_ID_UNMAP			= 0x00050000,
	TEE_CMD_ID_INITIALIZE			= 0x00FF0000,
};

/* version */
struct tee_get_version_info {
	char		productid[TEE_PRODUCT_ID_LEN];
	u32		versionmci;
	u32		versionso;
	u32		versionmclf;
	u32		versioncontainer;
	u32		versionmcconfig;
	u32		versiontlapi;
	u32		versiondrapi;
	u32		versioncmp;
};

struct tee_cmd_get_version_info {
	u32		reserved[4];
};

struct tee_resp_get_version_info {
	struct tee_get_version_info	versioninfo;
};

struct tee_cmd_open {
	u32		servicephyaddrhi;
	u32		servicephyaddrlo;
	u32		servicelen;
	u32		tcibufphyaddrhi;
	u32		tcibufphyaddrlo;
	u32		tcibuflen;
};

struct tee_resp_open {
	u32		sessionid;
};

struct tee_cmd_close {
	u32		sessionid;
};
struct tee_resp_close {
	u32		reserved[4];
};

struct tee_cmd_map {
	u32		sessionid;
	u32		memphyaddrhi;
	u32		memphyaddrlo;
	u32		membuflen;
};

struct tee_resp_map {
	u32		securevirtadr;
};

struct tee_cmd_unmap {
	u32		sessionid;
	u32		securevirtadr;
};

struct tee_resp_unmap {
	u32		reserved[4];
};

/* command/response buffer	*/
union tee_cmd_buf {
	struct tee_cmd_open			cmdopen;
	struct tee_cmd_get_version_info		cmdgetversioninfo;
	struct tee_cmd_close			cmdclose;
	struct tee_cmd_map			cmdmap;
	struct tee_cmd_unmap			cmdunmap;
	struct tee_resp_open			respopen;
	struct tee_resp_get_version_info	respgetversioninfo;
	struct tee_resp_close			respclose;
	struct tee_resp_map			respmap;
	struct tee_resp_unmap			respunmap;
	u32					padding[32];
};

union tee_cmd_buf	cmdbuf;
#endif
