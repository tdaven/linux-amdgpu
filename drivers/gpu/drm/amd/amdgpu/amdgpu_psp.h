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

#ifndef __AMDGPU_PSP_H__
#define __AMDGPU_PSP_H__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#define G2P_CMD_STATUS_MASK		0x0000FFFF
#define G2P_CMD_ID_MASK			0x000F0000

#define MAX_FRAMES			128
#define RING_BUF_FRAME_SIZE		32
#define RING_BUF_MAX_SIZE		(RING_BUF_FRAME_SIZE * MAX_FRAMES)

#define G2P_COMM_FLAG_RESPONSE		(0x80000000)
#define G2P_COMM_MAX_CLIENT_TYPES	(16)

#define PSP_MP0_SW_INT_ACK_VALUE	0x00000001

#define COMMAND_RESP_TIMEOUT		500	/* timeout in milliseconds */

extern int hdcpss_init(void *);

enum g2p_comm_cmd_id {
	G2P_COMM_CMD_ID_INIT_RBI_RING	= 0x00010000,
	G2P_COMM_CMD_ID_INIT_GPCOM_RING	= 0x00020000,
	G2P_COMM_CMD_ID_DESTROY_RINGS	= 0x00030000,
	G2P_COMM_CMD_ID_CAN_INIT_RINGS	= 0x00040000,
	G2P_COMM_CMD_ID_MAX		= 0x000F0000,
};

typedef int (*clientcallbackfunc)(void *notification_data);

struct g2p_comm_rb_frame {
	u32	cmd_buff_addr_hi;
	u32	cmd_buff_addr_lo;
	u32	cmd_buf_size;
	u32	resv1;
	u32	fence_addr_hi;
	u32	fence_addr_lo;
	u32	fence;
	u32	resv2;
};

struct client_context {
	int	type;
	clientcallbackfunc	callbackfunc;
};

struct amdgpu_psp {
	struct amdgpu_device		*adev;
	struct mutex			psp_mutex;
	wait_queue_head_t		psp_queue;
	struct g2p_comm_rb_frame	*phys_addr;
	struct g2p_comm_rb_frame	*virt_addr;
	struct amdgpu_irq_src		irq;
	struct client_context		*client_data[G2P_COMM_MAX_CLIENT_TYPES];
	u32				is_resp_recvd;
	u64				*fence_addr;
	const struct firmware		*fw;
};

extern const struct amd_ip_funcs psp_ip_funcs;

#endif	/* __AMDGPU_PSP_H__ */
