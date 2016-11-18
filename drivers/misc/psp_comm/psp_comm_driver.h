/*
* psp_comm_driver.h - communication driver between host and secure processor
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

#ifndef _PSP_COMM_DRIVER_H_
#define _PSP_COMM_DRIVER_H_
#include <linux/version.h>
#include <linux/highmem.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/vmalloc.h>

/* define types */
#if !defined(TRUE)
#define TRUE (1 == 1)
#endif

#if !defined(FALSE)
#define FALSE (1 != 1)
#endif

#define INVALID_ORDER           ((u32)(-1))
/* pci device/region */
#define	AMD_VENDOR_ID			(0x1022)
#define	CCP_CARRIZO_DEVICE_ID		(0x1578)
#define	PSP_BAR_NUM			(0x2)
#define MSIX_VECTORS			(0x2)
#define PSP_COMM_API_VERSION		(0x00000001)

/* secure response */
#define PSP_COMM_FLAG_RESPONSE		(0x80000000)
#define PSP_COMM_RESPONSE_TIMEOUT	(0xffff)
#define PSP_COMMAND_WAIT_TIMEOUT	(0x5000)
/* c2pmailbox registers */
#define PSP_COMM_C2PMSG_17		(0x0044)
#define PSP_COMM_C2PMSG_18		(0x0048)
#define PSP_COMM_C2PMSG_19		(0x004c)
#define PSP_COMM_C2PMSG_20		(0x0050)
#define PSP_COMM_C2PMSG_21		(0x0054)
#define PSP_COMM_C2PMSG_22		(0x0058)
#define PSP_COMM_C2PMSG_23		(0x005c)
#define PSP_COMM_P2CMSG_INTSTS		(0x0094)

#define PSP_COMM_MAX_CLIENT_TYPES	(16)
#define PSP_COMM_MAX_QUEUES_ENTRIES     (16)
#define PSP_COMM_CMD_BUF_MAX_SIZE	(128)
#define PSP_COMM_MSB_MASK		(1<<31)
#define PSP_COMM_CLIENT_TYPE_MASK	(0xF)
#define PSP_COMM_CMD_ERROR_MASK		(0xFFFF)
#define PSP_COMM_CLIENT_TYPE_SHIFT	(24)

#define	PSP_COMM_NO_NOTIFICATION_PENDING (0x0)
#define	PSP_COMM_MAX_OUT_RDPTR_VALUE	(0xf)
#define	PSP_COMM_MAX_OUT_WRTPTR_VALUE	(0xf)
#define MAX_SESSIONS_SUPPORTED		(0x10) /* To be same as in tee.h */

enum psp_comm_cmd_id {
	PSP_COMM_CMD_ID_INITIALIZE = 0x00FF0000
} psp_comm_cmd_id;

enum psp_comm_client_type {
	PSP_COMM_CLIENT_TYPE_TEE = 0,
/* New clients to be added here */
	PSP_COMM_CLIENT_TYPE_INVALID
} psp_comm_client_type;


typedef  int (*clientcallbackfunc)(void *notification_data);

struct client_context {
	int type;
	clientcallbackfunc callbackfunc;
};

struct psp_comm_drv_data {
	u32				command_status;
	wait_queue_head_t		command_wait;
	struct mutex			psp_comm_lock;
	struct workqueue_struct		*psp_comm_workqueue;
	struct work_struct		work;
	struct psp_comm_buf		*psp_comm_virtual_addr;
	struct psp_comm_buf		*psp_comm_physical_addr;
	struct client_context		*client_data[PSP_COMM_MAX_CLIENT_TYPES];
	u32				psp_init_done;
};

struct psp_comm_drv_data psp_comm_data;

struct psp_comm_notif {
	u32             session_id;
	u32             payload;
	u32             client_type;
	u32             reserved[1];
};

struct psp_comm_buf {
	u32                     bufsize;
	u32                     bufversion;
	u32                     inqueueelements;
	u32                     outqueueelements;
	u32                     reserved[12];
	u8                      cmdbuf[PSP_COMM_CMD_BUF_MAX_SIZE];
	struct psp_comm_notif   inqueue[PSP_COMM_MAX_QUEUES_ENTRIES];
	struct psp_comm_notif   outqueue[PSP_COMM_MAX_QUEUES_ENTRIES];
};

int psp_comm_pci_probe(struct pci_dev *dev, const struct pci_device_id *id);
void psp_comm_pci_remove(struct pci_dev *dev);

int psp_comm_init(void);
void psp_comm_write_command(u32 cmd, u32 high, u32 low);
void psp_comm_trigger_interrupt(u32 data);
void psp_comm_acknowledge_interrupt(void);

void psp_comm_write_out_queue_rdptr(u32 data);
u32 psp_comm_read_out_queue_rdptr(void);
u32 psp_comm_read_out_queue_wrptr(void);
u32 psp_comm_read_in_queue_wrptr(void);
u32 psp_comm_read_cmdresp_register(void);

void psp_comm_deallocate_memory(u64 addr, u32 requestedsize);
struct device *psp_comm_get_device(void);
void psp_comm_wait_for_status(void);

#endif
