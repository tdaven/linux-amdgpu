/*
* tee_driver.h - tee api for user space
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
#ifndef _TEE_DRIVER_H_
#define _TEE_DRIVER_H_

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include "tee_comm.h"

#define TEE_DRV_MOD_DEVNODE		"tee"
#define TEE_DRV_MOD_DEVNODE_FULLPATH	"/dev/" TEE_DRV_MOD_DEVNODE
#define TEE_CLIENT_TYPE			0
#define TEE_RESPONSE_TIMEOUT		400
#define TEE_SESSION_BUSY		0x1C
#define TEE_GP_TA_EXIT			303

enum tee_notify_status {
	TEE_NOTIFY_NONE = 0x0,
	TEE_NOTIFY_DONE = 0x1,
	TEE_NOTIFY_CLEAR = 0x2,
};

/* msp detail */
struct tee_map {
	uint64_t	useraddr;
	uint64_t	length;
};

/* open/close session */
union tee_open_session {
	struct {
		struct tee_map tl;
		struct tee_map tci;
	} in;
	struct {
		uint64_t sessionid;
	} out;
};

union tee_close_session {
	struct {
		uint64_t sessionid;
	} in;
	struct {
	} out;
};

/* map buffer */
union tee_map_buffer {
	struct {
		uint64_t sessionid;
		struct tee_map map_buffer;
	} in;
	struct {
		uint64_t securevirtual;
	} out;
};

union tee_unmap_buffer {
	struct {
		uint32_t sessionid;
		uint32_t securevirtual;
	} in;
	struct {
	} out;
};

/* notify/wait-for-notify */
union tee_notify {
	struct {
		uint64_t sessionid;
	} in;
	struct {
	} out;
};

union tee_waitfornotification {
	struct {
		uint64_t sessionid;
		int64_t timeout;
	} in;
	struct {
	} out;
};

/*  version info */
union tee_version_info {
	struct {
	} in;
	struct {
		char productid[TEE_PRODUCT_ID_LEN];
		uint32_t versionmci;
		uint32_t versionso;
		uint32_t versionmclf;
		uint32_t versioncontainer;
		uint32_t versionmcconfig;
		uint32_t versiontlapi;
		uint32_t versiondrapi;
		uint32_t versioncmp;
	} out;
};

/* session error */
union tee_get_session_error {
	struct {
		uint64_t sessionid;
	} in;
	struct {
		uint64_t error;
	} out;
};

/* session error */
enum tee_error {
	TEE_ERR_NONE = 0x0,
	TEE_ERR_UNKNOWN_DEVICE = 0x7,
	TEE_ERR_UNKNOWN_SESSION = 0x8,
	TEE_ERR_UNKNOWN_TCI = 0x9,
	TEE_ERR_INVALID_PARAMETER = 0x11,
};

struct tee_session_context {
	u64 sessionid;
	u64 map_virtaddr[MAX_BUFFERS_MAPPED];
	u64 map_secaddr[MAX_BUFFERS_MAPPED];
	u64 map_length[MAX_BUFFERS_MAPPED];
	wait_queue_head_t wait;
	u32 last_err;
	u32 status;
};

struct tee_tci_context {
	u32 uid;
	u32 sessionid;
	u64 virtaddr;
	u64 useraddr;
	u64 length;
};

/*
 * Used to hold user and kernel space mapping info
 * for the buffer whose memory is allocated by tee_mmap().
 * mmap_list: Beginning of list of mmaped buffers
 * length: Page aligned length. Used to free mmaped buffers
 * This length should not be used in cmd buffers
 */
struct wsm_mapping {
	void *k_vaddr;
	void *u_vaddr;
	void **mmap_list;
	u64	 length;
	struct list_head node;
};

struct tee_instance {
	struct semaphore sem;
	struct wsm_mapping *head;
	u64 uid;
};

struct tee_callback_data {
	u32 session_id;
	u32 payload;
};

struct tee_driver_data {
	struct tee_session_context	*tee_session;
	struct tee_tci_context		*tee_tci;
	struct semaphore		tee_sem;
	u32				tee_instance_counter;
	u64				tee_uid;
	u32				tee_error;
};

struct tee_driver_data	tee_drv_data;

#define TEE_OPEN_SESSION			(0x1)
#define TEE_CLOSE_SESSION			(0x2)
#define TEE_MAP_BUFFER				(0x3)

#define TEE_UNMAP_BUFFER			(0x4)
#define TEE_NOTIFY				(0x5)
#define TEE_WAIT_FOR_NOTIFICATION		(0x6)

#define TEE_VERSION_INFO			(0x7)
#define TEE_SESSION_ERROR			(0x8)

#define TEE_DEV_MAGIC				(0xAA)

#define TEE_KMOD_IOCTL_OPEN_SESSION	_IOWR(TEE_DEV_MAGIC, (0x1), uint64_t)
#define TEE_KMOD_IOCTL_CLOSE_SESSION	_IOWR(TEE_DEV_MAGIC, (0x2), uint64_t)
#define TEE_KMOD_IOCTL_MAP_BUFFER	_IOWR(TEE_DEV_MAGIC, (0x3), uint64_t)

#define TEE_KMOD_IOCTL_UNMAP_BUFFER	_IOWR(TEE_DEV_MAGIC, (0x4), uint64_t)
#define TEE_KMOD_IOCTL_NOTIFY		_IOWR(TEE_DEV_MAGIC, (0x5), uint64_t)
#define TEE_KMOD_IOCTL_WAIT_FOR_NOTIFICAION \
	_IOWR(TEE_DEV_MAGIC, (0x6), uint64_t)

#define TEE_KMOD_IOCTL_VERSION_INFO	_IOWR(TEE_DEV_MAGIC, (0x7), uint64_t)
#define TEE_KMOD_IOCTL_SESSION_ERROR	_IOWR(TEE_DEV_MAGIC, (0x8), uint64_t)

#define DRV_INFO_NOTIFICATION	0x15

int tee_open(struct inode *pinode, struct file *pfile);
int tee_release(struct inode *pinode, struct file *pfile);
int tee_mmap(struct file *pfile, struct vm_area_struct *pvmarea);
long tee_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg);

ssize_t tee_status(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf);
ssize_t tee_reset(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf,
		size_t count);
#endif

