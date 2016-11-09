/*
* tee_driver.c - TEE client (teeapi) driver
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

#include "tee_driver.h"
#include <linux/psp_comm_if.h>

static struct kobject *tee_kobj;

static const struct file_operations tee_fileopertaions = {
	.owner          = THIS_MODULE,
	.open           = tee_open,
	.release        = tee_release,
	.unlocked_ioctl = tee_ioctl,
	.compat_ioctl   = tee_ioctl,
	.mmap           = tee_mmap,
};

static struct miscdevice tee_device = {
	.name   = TEE_DRV_MOD_DEVNODE,
	.minor  = MISC_DYNAMIC_MINOR,
	.fops   = &tee_fileopertaions,
};

static struct kobj_attribute debug_attr =
__ATTR(status, S_IRUGO|S_IWUSR,
		(void *)tee_status, (void *)tee_reset);


static struct attribute *sysfs_attrs[] = {
	&debug_attr.attr,
	NULL
};

static struct attribute_group tee_attribute_group = {
	.attrs = sysfs_attrs
};

static inline u32 get_last_err(int session_index)
{
	return tee_drv_data.tee_session[session_index].last_err;
}

/* Caller need to hold lock while making call to this funciton */
static inline void set_last_err(int session_index, u32 err)
{
	pr_debug("%s:err=%d\n", __func__, (int)err);
	tee_drv_data.tee_session[session_index].last_err = err;
}

u64 tee_allocate_memory(u32 requestedsize)
{
	void *addr;

	pr_debug(" %s : req-sz 0x%llx\n",
			__func__, (u64)requestedsize);
	do {
		/* pages should be 4K aligned */
		addr = (void *)__get_free_pages(GFP_KERNEL,
					get_order(requestedsize));
		if (NULL == addr) {
			pr_err(
			" %s : addr(bulk-comm/map/tci/user) failed 0x%llx\n",
				__func__, (u64)addr);
			break;
		}
	} while (FALSE);
	pr_debug(" %s : addr(bulk-comm/map/tci/user) addr 0x%llx\n",
			__func__, (u64)addr);
	return (u64)addr;
}

void tee_free_memory(void *addr, u32 requestedsize)
{
	if (addr) {
		pr_debug("free (bulk-comm/map/tci/user) addr 0x%llx\n",
				(u64)addr);
		free_pages((u64)addr, get_order(requestedsize));
	}
}

/*
 * Deletes wsm_map entry from linked list pointed by head.
 * mmap_list is updated with new head.
 */
static inline void tee_del_mapping(struct wsm_mapping *wsm_map)
{
	struct wsm_mapping *curr_head, *new_head;
	int ret = 0;

	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		pr_debug(" %s : down_interruptible failed %d", __func__, ret);

	BUG_ON(wsm_map == NULL);
	curr_head = (struct wsm_mapping *)*wsm_map->mmap_list;
	new_head = curr_head;

	BUG_ON(curr_head == NULL);

	pr_debug("%s:Head is at 0x%lx\n", __func__, (unsigned long)curr_head);

	if (list_empty(&curr_head->node)) {
		list_del(&curr_head->node);
		*wsm_map->mmap_list = NULL;
		up(&tee_drv_data.tee_sem);
		return;
	}

	if (curr_head == wsm_map)
		new_head = list_next_entry(curr_head, node);

	pr_debug("%s:New Head is @ 0x%lx\n", __func__, (unsigned long)new_head);

	list_del(&wsm_map->node);

	*wsm_map->mmap_list = new_head;

	up(&tee_drv_data.tee_sem);
}

/*
 * wsm_map entry added to the linked list pointed by head.
 */
static inline void tee_add_mapping(struct wsm_mapping *head,
					    struct wsm_mapping *wsm_map)
{
	int ret = 0;

	BUG_ON(head == NULL);

	if (wsm_map == NULL)
		return;

	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		pr_debug(" %s : down_interruptible failed %d", __func__, ret);

	list_add_tail(&wsm_map->node, &head->node);

	up(&tee_drv_data.tee_sem);
}

/*
 * Retrieves mmap info for given mmaped user space address
 * If user_addr is not mmaped buffer then NULL is returned
 * Caller function should hold lock
 */
static struct wsm_mapping *tee_search_mapping(struct wsm_mapping *head,
						void *user_addr)
{
	struct wsm_mapping *map = head;

	if (head == NULL)
		return NULL;

	pr_debug("%s:Searching mapping info for ua 0x%llx\n", __func__,
			(u64)user_addr);

	/* Check if head itself has the required map info */
	if (map->u_vaddr == user_addr)
		return map;

	list_for_each_entry(map, &head->node, node) {
		pr_debug("map 0x%lx, ua 0x%lx, kva 0x%lx, len %ld\n",
				(unsigned long)map,
				(unsigned long)map->u_vaddr,
				(unsigned long)map->k_vaddr,
				(unsigned long)map->length);
		if (map->u_vaddr == user_addr)
			return map;
	}

	pr_err(" %s failed. head is at 0x%lx\n", __func__,
			(unsigned long) head);
	return NULL;
}

/*
 * This routine is called at the end of munmap system call.
 * vm_private will hold details required to free memory
 */
static void tee_vma_close(struct vm_area_struct *vma)
{
	unsigned long vsize = vma->vm_end - vma->vm_start;
	struct wsm_mapping *wsm_map = vma->vm_private_data;

	if (wsm_map == NULL) {
		pr_err("  [%s] err: mapping not found: uva=%lx, size=%lx\n",
				__func__, vma->vm_start, vsize);
		return;
	}
	pr_debug("%s:Freeing uva 0x%lx using kva 0x%lx\n",
			__func__, (unsigned long)wsm_map->u_vaddr,
			(unsigned long)wsm_map->k_vaddr);

	tee_free_memory(wsm_map->k_vaddr, (u32)wsm_map->length);
	pr_debug("%s:Deleting from mmap list\n", __func__);
	tee_del_mapping(wsm_map);
	vfree(wsm_map);
	vma->vm_private_data = NULL;
}

static const struct vm_operations_struct tee_vma_ops = {
	.close          = tee_vma_close,
};

static inline int tee_range_check(u32 location)
{
	if (location < MAX_SESSIONS_SUPPORTED)
		return 0;
	tee_drv_data.tee_error = TEE_ERR_UNKNOWN_SESSION;
	pr_err(" %s : session exceed. location = %d\n",
			__func__, location);
	return -1;
}

static inline int tee_session_location(u32 session)
{
	int i;

	for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
		if (tee_drv_data.tee_session[i].sessionid == session)
			break;
	}
	if (tee_range_check(i) < 0)
		return -1;
	return i;
}

static void tee_terminate_wait(u32 session)
{
	int ret = 0;
	/* make sure waitnotification ends */
	if (tee_drv_data.tee_session[session].status == TEE_NOTIFY_DONE) {
		pr_debug(" %s : wait notification in progress\n",
				__func__);
		tee_drv_data.tee_session[session].status = TEE_NOTIFY_CLEAR;
		up(&tee_drv_data.tee_sem);
		while (tee_drv_data.tee_session[session].status
				== TEE_NOTIFY_NONE)
			;
		ret = down_interruptible(&tee_drv_data.tee_sem);
		if (0 != ret)
			pr_err(" %s : down_interruptible failed %d",
					__func__, ret);
	}
}

static void tee_clean_tci(u32 location)
{
	if (tee_drv_data.tee_tci[location].useraddr != 0) {
		tee_free_memory(
			(void *)tee_drv_data.tee_tci[location].virtaddr,
			get_pagealigned_size(
				tee_drv_data.tee_tci[location].length));
	}
	tee_drv_data.tee_tci[location].virtaddr = 0x0;
	tee_drv_data.tee_tci[location].uid = 0x0;
	tee_drv_data.tee_tci[location].sessionid = 0x0;
	tee_drv_data.tee_tci[location].length = 0x0;
	tee_drv_data.tee_tci[location].useraddr = 0x0;
}

static void tee_clean_map(u32 session, u32 location)
{
	tee_drv_data.tee_session[session].map_virtaddr[location] = 0x0;
	tee_drv_data.tee_session[session].map_secaddr[location] = 0x0;
	tee_drv_data.tee_session[session].map_length[location] = 0x0;
}

static int tee_close_session(u32 session)
{
	cmdbuf.cmdclose.sessionid = session;
	return psp_comm_send_buffer(TEE_CLIENT_TYPE,
		(union tee_cmd_buf *)&cmdbuf,
		sizeof(union tee_cmd_buf), TEE_CMD_ID_CLOSE_SESSION);
}

int tee_driver_init(void)
{
	int     ret = 0;

	pr_debug(" %s :\n", __func__);
	do {
		tee_drv_data.tee_session = vzalloc(
				sizeof(struct tee_session_context)
				*MAX_SESSIONS_SUPPORTED);
		if (0 == tee_drv_data.tee_session) {
			pr_err(" %s : alloc(session) failed\n",
					__func__);
			ret = -ENOMEM;
			break;
		}
		pr_debug(" %s : alloc(session) addr 0x%llx\n",
				__func__, (u64)tee_drv_data.tee_session);
		tee_drv_data.tee_tci = vzalloc(
				sizeof(struct tee_tci_context)
				*MAX_SESSIONS_SUPPORTED);
		if (0 == tee_drv_data.tee_tci) {
			pr_debug(" %s : alloc(tci-s) failed\n",
					__func__);
			vfree(tee_drv_data.tee_session);
			ret = -ENOMEM;
			break;
		}
		pr_debug(" %s : alloc(tci-s) addr 0x%llx\n",
				__func__, (u64)tee_drv_data.tee_tci);
	} while (FALSE);
	pr_info(" %s : tee driver initialized\n", __func__);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return (int)ret;
}

void tee_driver_exit(void)
{
	int i, j;

	pr_debug(" %s :\n", __func__);
	for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
		if (tee_drv_data.tee_session[i].sessionid != 0x0) {
			pr_debug(" %s : sid: 0x%llx close ",
			__func__, tee_drv_data.tee_session[i].sessionid);
			tee_close_session(tee_drv_data.tee_session[i].
								sessionid);
			tee_drv_data.tee_session[i].sessionid = 0x0;
			for (j = 0; j < MAX_BUFFERS_MAPPED; j++)
				tee_clean_map(i, j);
		}
	}
	for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++)
		tee_clean_tci(i);
	if (tee_drv_data.tee_tci) {
		pr_debug(" %s : free(tci-s) addr 0x%llx\n",
				__func__, (u64)tee_drv_data.tee_tci);
		vfree(tee_drv_data.tee_tci);
		tee_drv_data.tee_tci = 0x0;
	}
	if (tee_drv_data.tee_session) {
		pr_debug(" %s : free(session) addr 0x%llx\n",
				__func__, (u64)tee_drv_data.tee_session);
		vfree(tee_drv_data.tee_session);
		tee_drv_data.tee_session = 0x0;
	}
}

int tee_open(struct inode *pinode, struct file *pfile)
{
	int ret = 0;
	struct tee_instance *instance;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		if (tee_drv_data.tee_instance_counter == 0) {
			ret = tee_driver_init();
			if (0 != ret)
				break;
		}
		instance = vzalloc(sizeof(struct tee_instance));
		if (0 == instance) {
			pr_err(" %s : alloc(instance) failed\n",
					__func__);
			ret = -ENOMEM;
			up(&tee_drv_data.tee_sem);
			tee_driver_exit();
			break;
		}
		pr_debug(" %s : alloc(instance) addr 0x%llx\n",
				__func__,
				(u64)instance);
		sema_init(&instance->sem, 0x1);
		tee_drv_data.tee_instance_counter++;
		instance->uid = ++tee_drv_data.tee_uid;
		pfile->private_data = instance;
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return (int)ret;
}

int tee_release(struct inode *pinode, struct file *pfile)
{
	int ret = 0;
	struct tee_instance *instance =
		(struct tee_instance *)pfile->private_data;
	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		if (tee_drv_data.tee_instance_counter == 0)
			break;
		if (instance) {
			pr_debug(" %s : free(instance) addr 0x%llx\n",
					__func__, (u64)instance);
			vfree(instance);
			instance = 0x0;
		}
		if (tee_drv_data.tee_instance_counter == 0)
			tee_driver_exit();
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return (int)ret;
}

int tee_mmap(struct file *pfile, struct vm_area_struct *pvmarea)
{
	void *virtaddr = 0;
	void *physaddr = 0;
	u64 requestedsize = pvmarea->vm_end - pvmarea->vm_start;
	int ret = 0;
	u64 addr;
	struct wsm_mapping *wsm_map = NULL;
	struct tee_instance *instance =
		(struct tee_instance *)pfile->private_data;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&instance->sem);
	if (0 != ret)
		return ret;
	do {
		if (requestedsize == 0x0) {
			ret = -ENOMEM;
			break;
		}
		addr = tee_allocate_memory(requestedsize);
		virtaddr = (void *)addr;
		physaddr = (void *)virt_to_phys((void *)addr);
		if (0 == virtaddr) {
			pr_err(" %s : alloc failed\n", __func__);
			ret = -ENOMEM;
			break;
		}
		/* mark it non-cacheable */
		pvmarea->vm_flags |= VM_IO |
			(VM_DONTEXPAND | VM_DONTDUMP);
		pvmarea->vm_page_prot =
			pgprot_noncached(pvmarea->vm_page_prot);
		ret = (int)remap_pfn_range(
				pvmarea,
				(pvmarea->vm_start),
				addrtopfn(physaddr),
				requestedsize,
				pvmarea->vm_page_prot);

		if (0 != ret) {
			pr_err(" %s : rfr failed\n", __func__);
			tee_free_memory((void *)addr, requestedsize);
			break;
		}
		pr_debug(" %s : head is at 0x%lx\n", __func__,
					    (unsigned long) instance->head);

		wsm_map = vzalloc(sizeof(struct wsm_mapping));
		if (wsm_map == NULL) {
			ret = -ENOMEM;
			break;
		}

		if (instance->head == NULL) {
			INIT_LIST_HEAD(&wsm_map->node);
			instance->head = wsm_map;
		}

		wsm_map->k_vaddr = virtaddr;
		wsm_map->u_vaddr = (void *)pvmarea->vm_start;
		wsm_map->length  = requestedsize;
		wsm_map->mmap_list  = (void **)&instance->head;

		tee_add_mapping(instance->head, wsm_map);

		pvmarea->vm_ops = &tee_vma_ops;
		pvmarea->vm_private_data = wsm_map;

		pr_debug(" %s : vm pdata is at 0x%lx\n", __func__,
			(unsigned long) pvmarea->vm_private_data);
	} while (FALSE);
	up(&instance->sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return (int)ret;
}

ssize_t tee_reset(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf,
		size_t count)
{
	int ret = 0, i, j;
	int sessionid;

	pr_debug(" %s :\n", __func__);
	ret = sscanf(buf, "%du", &sessionid);
	if (0 != ret)
		return ret;
	pr_debug(" %s : sid: %d to be cleaned; echo 999 to clean all\n",
		__func__, sessionid);
	if (tee_drv_data.tee_instance_counter) {
		ret = down_interruptible(&tee_drv_data.tee_sem);
		if (0 != ret) {
			pr_err(" %s : down_interruptible failed %d",
					__func__, ret);
			return 0;
		}
		do {
			pr_debug(" %s : instance present %d\n",
					__func__,
					tee_drv_data.tee_instance_counter);
			if ((!tee_drv_data.tee_session) ||
					(!tee_drv_data.tee_tci)) {
				pr_err(" null data\n");
				break;
			}
			for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
				if ((tee_drv_data.tee_session[i].sessionid
								== sessionid) ||
					((tee_drv_data.tee_session[i]
						.sessionid != 0x0) &&
						 (sessionid == 999))) {
					pr_debug(
					" %s : sid: 0x%llx pos:%d close\n",
								__func__,
						tee_drv_data.tee_session[i]
								.sessionid,
						i);
					tee_terminate_wait(i);
					tee_close_session(
					tee_drv_data.tee_session[i].sessionid);
					tee_drv_data.tee_session[i]
							.sessionid = 0x0;
					for (j = 0;
						j < MAX_BUFFERS_MAPPED; j++) {
							tee_clean_map(i, j);
						}
						tee_clean_tci(i);
					}
				}
		} while (FALSE);
			up(&tee_drv_data.tee_sem);
	} else
		pr_debug(" %s : no instance present\n",
				__func__);
	return count;
}

static void tee_debug_session(void)
{
	int i, j;

	for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
		if (tee_drv_data.tee_session[i].sessionid != 0x0) {
			pr_debug(" %s : sid: 0x%llx position:%d\n",
					__func__,
			tee_drv_data.tee_session[i].sessionid, i);
			for (j = 0; j < MAX_BUFFERS_MAPPED; j++) {
				if (tee_drv_data.tee_session[i]
						.map_virtaddr[j])
					pr_debug(
			" %s : sid: 0x%llx position:%d mapped addr 0x%llx\n",
							__func__,
					tee_drv_data.tee_session[i].
					sessionid,
					i,
					(u64)tee_drv_data.tee_session[i].
					map_virtaddr[j]);
				pr_debug(
					" %s : position:%d len 0x%llx\n",
					__func__, j,
					tee_drv_data.tee_session[i].
					map_length[j]);
			}
			if (tee_drv_data.tee_tci[i].virtaddr)
				pr_debug(
				" %s : sid: 0x%llx position:%d\n",
				__func__,
				tee_drv_data.tee_session[i].sessionid, i);
			pr_debug(
				" %s : tci addr 0x%llx len 0x%llx\n",
				__func__,
				tee_drv_data.tee_tci[i].virtaddr,
				tee_drv_data.tee_tci[i].length);
		}
	}
}

ssize_t tee_status(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	int ret = 0;

	pr_debug(" %s :\n", __func__);

	if (tee_drv_data.tee_instance_counter) {
		pr_debug(" %s : instance present %d\n",
				__func__,
				tee_drv_data.tee_instance_counter);
		ret = down_interruptible(&tee_drv_data.tee_sem);
		if (0 != ret) {
			pr_err(" %s : down_interruptible failed %d",
					__func__, ret);
			return 0;
		}
		do {
			if ((!tee_drv_data.tee_session) ||
					(!tee_drv_data.tee_tci)) {
				pr_err(" null data\n");
				break;
			}
			tee_debug_session();
		} while (FALSE);
		up(&tee_drv_data.tee_sem);
	} else
		pr_debug(" %s : no instance present\n", __func__);
	return 0;
}

static int handle_open_session(struct tee_instance *instance,
		union tee_open_session *puserparams)
{
	int ret = 0, i = MAX_SESSIONS_SUPPORTED;
	union tee_open_session params;
	struct wsm_mapping *tci_map, *tl_map;
	u64 tci_addr = 0;
	u64 tci_len = 0;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) ||
				(!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			ret = -EINVAL;
			break;
		}
		pr_debug("params.in.tci_useraddr 0x%llx\n",
				params.in.tci.useraddr);
		pr_debug("params.in.tl.useraddr 0x%llx\n",
				params.in.tl.useraddr);

		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_tci[i].uid == 0x0) {
				tee_drv_data.tee_tci[i].uid = instance->uid;
				pr_debug("UID=%lld; i =%d\n", instance->uid, i);
				break;
			}
		}

		ret = tee_range_check(i);
		if (ret != 0)
			break;

		tl_map = tee_search_mapping(instance->head,
					(void *)params.in.tl.useraddr);
		if (tl_map == NULL) {
			ret = -EINVAL;
			break;
		}

		tci_map = tee_search_mapping(instance->head,
					(void *)params.in.tci.useraddr);
		/*
		 * tci buffer is allocated using malloc
		 * We need physically contiguous memory to communicate to
		 * TEE device.
		 * Create a temp buffer and use it to communicate with TEE
		 * device
		 */
		if (tci_map == NULL) {
			tci_len = params.in.tci.length;
			tci_addr = tee_allocate_memory(get_pagealigned_size(
						(u32)tci_len));
			if (0 == tci_addr) {
				pr_err(" %s : alloc failed\n", __func__);
				ret = -ENOMEM;
				break;
			}
		/*
		 * tci buffer is allocated using tee mmap call
		 * Can be directly used to communicate with TEE device
		 */
		} else {
			tci_addr = (u64)tci_map->k_vaddr;
			tci_len = tci_map->length;
			if (params.in.tci.length > tci_len) {
				pr_err(" %s : invalid tci length\n", __func__);
				ret = -EINVAL;
				break;
			}
		}
		cmdbuf.cmdopen.servicephyaddrhi = upper_32_bits(virt_to_phys(
						(void *)tl_map->k_vaddr));
		cmdbuf.cmdopen.servicephyaddrlo = lower_32_bits(virt_to_phys(
						(void *)tl_map->k_vaddr));
		cmdbuf.cmdopen.servicelen = tl_map->length;

		cmdbuf.cmdopen.tcibufphyaddrhi = upper_32_bits(virt_to_phys(
						(void *)tci_addr));
		cmdbuf.cmdopen.tcibufphyaddrlo = lower_32_bits(virt_to_phys(
						(void *)tci_addr));
		cmdbuf.cmdopen.tcibuflen = params.in.tci.length;

		flush_buffer((void *)tl_map->k_vaddr, tl_map->length);
		ret = psp_comm_send_buffer(TEE_CLIENT_TYPE,
				(union tee_cmd_buf *)&cmdbuf,
				sizeof(union tee_cmd_buf),
				TEE_CMD_ID_OPEN_SESSION);
		if (ret != 0)
			break;

		pr_debug(" sid %d\n", tee_drv_data.tee_tci[i].sessionid);
		params.out.sessionid = cmdbuf.respopen.sessionid;
		tee_drv_data.tee_tci[i].sessionid = params.out.sessionid;

		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_session[i].sessionid == 0x0) {
				tee_drv_data.tee_session[i].sessionid =
					params.out.sessionid;
				init_waitqueue_head(&tee_drv_data.
						tee_session[i].wait);
				tee_drv_data.tee_session[i].
						status = TEE_NOTIFY_NONE;
				break;
			}
		}

		tee_drv_data.tee_tci[i].virtaddr = tci_addr;
		tee_drv_data.tee_tci[i].length = tci_len;
		if (tci_map == NULL) {
			tee_drv_data.tee_tci[i].useraddr =
							params.in.tci.useraddr;
		}
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
	} while (FALSE);
	if (ret != 0 && i < MAX_SESSIONS_SUPPORTED)
		tee_drv_data.tee_tci[i].uid = 0;
	up(&tee_drv_data.tee_sem);
	pr_info(" %s : open session handled\n", __func__);
	return ret;
}

static int handle_close_session(struct tee_instance *instance,
		union tee_close_session *puserparams)
{
	int ret = 0, i, j;
	union tee_close_session params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			ret = -EINVAL;
			break;
		}

		if (params.in.sessionid == 0) {
			pr_err("%s:0x%d is not a valid session ID\n", __func__,
						(int)params.in.sessionid);
			ret = -EINVAL;
			break;
		}

		pr_debug("%s:Closing session ID %d.\n", __func__,
						    (int)params.in.sessionid);

		i = tee_session_location(params.in.sessionid);
		if (i < 0) {
			ret = i;
			break;
		}

try_close_session:
		ret = tee_close_session(params.in.sessionid);
		if (ret != 0) {
			pr_err("%s: Error while clossing sesssion %d.Erro %d\n"
				, __func__, (int)params.in.sessionid, ret);

			/*
			 * Set state to TEE_NOTIFY_DONE so that we are
			 * woken up if extra notification response
			 * arrives. Abort attempt to close session if
			 * signal is received.
			 */
			if ((ret & 0xFF) == TEE_SESSION_BUSY) {
				tee_drv_data.tee_session[i].status =
					TEE_NOTIFY_DONE;

				up(&tee_drv_data.tee_sem);
				ret = wait_event_interruptible_timeout(
						tee_drv_data
						.tee_session[i].wait,
						tee_drv_data
						.tee_session[i].status
						== TEE_NOTIFY_CLEAR,
						msecs_to_jiffies(5000));
				if (ret < 0)
					return ret;

				ret = down_interruptible(&tee_drv_data.tee_sem);
				if (0 != ret)
					return ret;

				tee_drv_data.tee_session[i].status =
					TEE_NOTIFY_NONE;

				goto try_close_session;
			}
			break;
		}

		for (j = 0; j < MAX_BUFFERS_MAPPED; j++)
			tee_clean_map(i, j);
		tee_clean_tci(i);
		tee_drv_data.tee_session[i].sessionid = 0x0;
		set_last_err(i, 0);

	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_version_info(union tee_version_info *puserparams)
{
	int ret = 0;
	union tee_version_info params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = psp_comm_send_buffer(TEE_CLIENT_TYPE,
				(union tee_cmd_buf *)&cmdbuf,
				sizeof(union tee_cmd_buf),
				 TEE_CMD_ID_GET_VERSION_INFO);
		if (ret != 0)
			break;

		params.out.versionmci =
			cmdbuf.respgetversioninfo.
			versioninfo.versionmci;
		params.out.versionso =
			cmdbuf.respgetversioninfo.
			versioninfo.versionso;
		params.out.versionmclf =
			cmdbuf.respgetversioninfo.
			versioninfo.versionmclf;
		params.out.versioncontainer =
			cmdbuf.respgetversioninfo.
			versioninfo.versioncontainer;
		params.out.versionmcconfig =
			cmdbuf.respgetversioninfo.
			versioninfo.versionmcconfig;
		params.out.versiontlapi =
			cmdbuf.respgetversioninfo.
			versioninfo.versiontlapi;
		params.out.versiondrapi =
			cmdbuf.respgetversioninfo.
			versioninfo.versiondrapi;
		params.out.versioncmp =
			cmdbuf.respgetversioninfo.
			versioninfo.versioncmp;
		memcpy(params.out.productid,
				cmdbuf.respgetversioninfo.
				versioninfo.productid,
				TEE_PRODUCT_ID_LEN);
		pr_debug(" tee version %s\n", params.out.productid);
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
		if (0 != ret)
			break;
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_session_error(union tee_get_session_error *puserparams)
{
	int ret = 0, i;
	union tee_get_session_error params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			ret = -EINVAL;
			pr_err(" null data\n");
			break;
		}

		i = tee_session_location(params.in.sessionid);
		if (i < 0) {
			ret = i;
			break;
		}
		params.out.error = get_last_err(i);
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
		if (0 != ret)
			break;

	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_map_buffer(struct tee_instance *instance,
				    union tee_map_buffer *puserparams)
{
	int ret = 0, i, j, k = 0;
	union tee_map_buffer params;
	struct wsm_mapping *map;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			ret = -EINVAL;
			break;
		}

		map = tee_search_mapping(instance->head,
					(void *)params.in.map_buffer.useraddr);
		if (map == NULL) {
			ret = -EINVAL;
			break;
		}

		cmdbuf.cmdmap.sessionid = params.in.sessionid;
		cmdbuf.cmdmap.membuflen = map->length;
		cmdbuf.cmdmap.memphyaddrhi = upper_32_bits(virt_to_phys(
							(void *)map->k_vaddr));
		cmdbuf.cmdmap.memphyaddrlo = lower_32_bits(virt_to_phys(
							(void *)map->k_vaddr));
		pr_debug(" %s : sid 0x%x map pa 0x%x length 0x%x\n",
				__func__,
				cmdbuf.cmdmap.sessionid,
				(u32)cmdbuf.cmdmap.memphyaddrlo,
				cmdbuf.cmdmap.membuflen);

		ret = psp_comm_send_buffer(TEE_CLIENT_TYPE,
				(union tee_cmd_buf *)&cmdbuf,
				sizeof(union tee_cmd_buf), TEE_CMD_ID_MAP);
		if (ret != 0)
			break;

		pr_debug("%s:sid 0x%x:Mapped pa 0x%x len 0x%x to sva 0x%x\n",
				__func__,
				(u32)params.in.sessionid,
				(u32)((virt_to_phys((void *)map->k_vaddr))
								& 0xffffffff),
				(u32) cmdbuf.cmdmap.membuflen,
				(u32) cmdbuf.respmap.securevirtadr);

		/* save map buffer context for given session */
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_session[i].sessionid ==
						params.in.sessionid) {
				for (j = 0; j < MAX_BUFFERS_MAPPED; j++) {
					if (tee_drv_data.tee_session[i]
								.map_secaddr[j]
							== 0x0) {
						tee_drv_data.tee_session[i]
							.map_virtaddr[j] =
							(u64)map->k_vaddr;
						tee_drv_data.tee_session[i]
							.map_secaddr[j] =
							cmdbuf.respmap.
							securevirtadr;
						tee_drv_data.tee_session[i]
							.map_length[j] =
							map->length;
						k = 1;
						break;
					}
				}
			}
			if (k == 1)
				break;
		}
		ret = tee_range_check(i);
		if (ret != 0)
			break;

		params.out.securevirtual = cmdbuf.respmap.securevirtadr;
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
		if (0 != ret)
			break;
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_unmap_buffer(union tee_unmap_buffer *puserparams)
{
	int ret = 0, i, j, k = 0;
	union tee_unmap_buffer params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		cmdbuf.cmdunmap.sessionid = params.in.sessionid;
		cmdbuf.cmdunmap.securevirtadr = params.in.securevirtual;
		ret = psp_comm_send_buffer(TEE_CLIENT_TYPE,
					 (union tee_cmd_buf *)&cmdbuf,
						sizeof(union tee_cmd_buf),
						TEE_CMD_ID_UNMAP);
		if (ret != 0)
			break;

		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_session[i].sessionid ==
							params.in.sessionid) {
				for (j = 0; j < MAX_BUFFERS_MAPPED; j++) {
					if (tee_drv_data.tee_session[i]
							.map_secaddr[j] ==
						params.in.securevirtual) {
						pr_info(
					" %s : map sva 0x%llx kva 0x%llx\n",
							__func__,
							tee_drv_data.
							tee_session[i].
							map_secaddr[j],
							tee_drv_data.
							tee_session[i].
							map_virtaddr[j]);
						tee_clean_map(i, j);
						k = 1;
						break;
					}
				}
			}
			if (k == 1)
				break;
		}
		ret = tee_range_check(i);
		if (ret != 0)
			break;

	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_notify(struct tee_instance *instance,
		union tee_notify *puserparams)
{
	int ret = 0, i, j;
	union tee_notify params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		pr_debug(" %s : sid 0x%llx\n",
				__func__, params.in.sessionid);
		i = tee_session_location(params.in.sessionid);
		if (i < 0) {
			ret = i;
			break;
		}

		/* Copy malloced TCI contents */
		if (tee_drv_data.tee_tci[i].useraddr != 0) {
			memcpy((void *)tee_drv_data.tee_tci[i].virtaddr,
				(void *)tee_drv_data.tee_tci[i].useraddr,
				tee_drv_data.tee_tci[i].length);
		}

		ret = psp_comm_send_notification(TEE_CLIENT_TYPE,
					(uint64_t *)&params.in.sessionid);
		if (0 == ret)
			tee_drv_data.tee_session[i].status = TEE_NOTIFY_DONE;

		for (j = 0; j < MAX_BUFFERS_MAPPED; j++) {
			flush_buffer((void *)tee_drv_data.tee_session[i]
							.map_virtaddr[j],
					tee_drv_data.tee_session[i]
							.map_length[j]);
		}
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_tci[i].sessionid ==
							params.in.sessionid) {
				flush_buffer((void *)tee_drv_data
							.tee_tci[i].virtaddr,
						tee_drv_data
							.tee_tci[i].length);
				invalidate_buffer((void *)tee_drv_data
							.tee_tci[i].virtaddr,
						tee_drv_data
							.tee_tci[i].length);
			}
		}
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_waitfornotification(struct tee_instance *instance,
		union tee_waitfornotification *puserparams)
{
	int ret = 0, i, j, timeout;
	union tee_waitfornotification params;

	pr_debug(" %s :\n", __func__);
	do {
		ret = down_interruptible(&tee_drv_data.tee_sem);
		if (0 != ret)
			break;
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret) {
			up(&tee_drv_data.tee_sem);
			break;
		}
		i = tee_session_location(params.in.sessionid);
		if (i < 0) {
			ret = i;
			up(&tee_drv_data.tee_sem);
			break;
		}
		/*
		 * TEE_GP_TA_EXIT is received for both success and failure
		 * cases.
		 * Also, this is not the last session error code received.
		 * This check is added to ensure that driver waits for
		 * notification response even after TEE_GP_TA_EXIT is received.
		 */
		if (get_last_err(i) != 0 && get_last_err(i) != TEE_GP_TA_EXIT) {
			ret = DRV_INFO_NOTIFICATION;
			up(&tee_drv_data.tee_sem);
			break;
		}

		if (tee_drv_data.tee_session[i].status != TEE_NOTIFY_CLEAR) {
			up(&tee_drv_data.tee_sem);
			pr_debug(" %s : sid 0x%llx\n", __func__,
					params.in.sessionid);
			pr_debug(" %s : waiting for interrupt\n", __func__);
			pr_debug(" %s : timeout 0x%llx\n", __func__,
					params.in.timeout);
			timeout = params.in.timeout;
			if (timeout < 0x0) {
				ret = wait_event_interruptible(tee_drv_data
						.tee_session[i].wait,
						tee_drv_data
						.tee_session[i].status
						== TEE_NOTIFY_CLEAR);
				if (ret != 0)
					break;
			} else {
				ret = wait_event_interruptible_timeout(
						tee_drv_data
						.tee_session[i].wait,
						tee_drv_data
						.tee_session[i].status
						== TEE_NOTIFY_CLEAR,
						msecs_to_jiffies(timeout));
				if (ret < 1) {
					pr_err("%s:Timeout occurred\n",
								__func__);
					tee_drv_data.tee_session[i].status =
								TEE_NOTIFY_NONE;
					ret = -ETIMEDOUT;
					break;
				}
			}
			ret = down_interruptible(&tee_drv_data.tee_sem);
			if (0 != ret)
				break;
		}
		tee_drv_data.tee_session[i].status = TEE_NOTIFY_NONE;

		/*
		 * As per specification, if payload is non zero return error
		 * GP Client lib expects low level driver to return
		 * DRV_INFO_NOTIFICATION even for TEE_GP_TA_EXIT session
		 * error code.
		 */
		if (get_last_err(i) != 0)
			ret = DRV_INFO_NOTIFICATION;

		for (j = 0; j < MAX_BUFFERS_MAPPED; j++) {
			invalidate_buffer(
					(void *)tee_drv_data.tee_session[i]
							.map_virtaddr[j],
					tee_drv_data.tee_session[i]
							.map_length[j]);
		}

		/* Copy contents to malloced TCI */
		if (tee_drv_data.tee_tci[i].useraddr != 0) {
			memcpy((void *)tee_drv_data.tee_tci[i].useraddr,
				(void *)tee_drv_data.tee_tci[i].virtaddr,
					tee_drv_data.tee_tci[i].length);
		}

		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_tci[i].sessionid ==
						params.in.sessionid) {
				flush_buffer(
						(void *)tee_drv_data
							.tee_tci[i].virtaddr,
						tee_drv_data
							.tee_tci[i].length);
				invalidate_buffer(
						(void *)tee_drv_data
							.tee_tci[i].virtaddr,
						tee_drv_data
							.tee_tci[i].length);
			}
		}
		up(&tee_drv_data.tee_sem);
	} while (FALSE);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

long tee_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	int ret;
	struct tee_instance *instance = pfile->private_data;

	pr_debug(" %s command %d :\n", __func__, _IOC_NR(cmd));
	switch (_IOC_NR(cmd)) {
	case TEE_OPEN_SESSION:
		ret = handle_open_session(instance,
				(union tee_open_session *)arg);
		break;
	case TEE_CLOSE_SESSION:
		ret = handle_close_session(instance,
				(union tee_close_session *)arg);
		break;
	case TEE_MAP_BUFFER:
		ret = handle_map_buffer(instance, (union tee_map_buffer *)arg);
		break;
	case TEE_UNMAP_BUFFER:
		ret = handle_unmap_buffer((union tee_unmap_buffer *)arg);
		break;
	case TEE_NOTIFY:
		ret = handle_notify(instance, (union tee_notify *)arg);
		break;
	case TEE_WAIT_FOR_NOTIFICATION:
		ret = handle_waitfornotification(instance,
				(union tee_waitfornotification *)arg);
		break;
	case TEE_VERSION_INFO:
		ret = handle_version_info((union tee_version_info *)arg);
		break;
	case TEE_SESSION_ERROR:
		ret = handle_session_error((union tee_get_session_error *)arg);
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	pr_debug(" %s : ret %d\n", __func__, ret);
	return (int)ret;
}

int tee_callbackfunc(void *notification_data)
{
	int i = 0, ret = 0;
	struct tee_callback_data *cb = NULL;

	if (notification_data == NULL)
		return -EINVAL;

	cb = (struct tee_callback_data *)notification_data;

	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;

	/* Retrieve Session ID */
	i = tee_session_location(cb->session_id);
	if (i < 0) {
		up(&tee_drv_data.tee_sem);
		return -EINVAL;
	}
	pr_debug("%s:i=%d; sid=%d\n", __func__, i, cb->session_id);
	if (cb->payload)
		set_last_err(i, cb->payload);

	/* This is to handle multiple callback for single McNotify */
	if (tee_drv_data.tee_session[i].status != TEE_NOTIFY_DONE) {
		up(&tee_drv_data.tee_sem);
		pr_debug("%s:sid=0x%x, payload=0x%x\n", __func__,
						cb->session_id, cb->payload);
		return -EINVAL;
	}
	tee_drv_data.tee_session[i].status = TEE_NOTIFY_CLEAR;
	wake_up_interruptible(&tee_drv_data.tee_session[i].wait);
	up(&tee_drv_data.tee_sem);
	return ret;
}

static int __init tee_init(
		void
		)
{
	int ret = -EIO;

	do {
		ret = misc_register(&tee_device);
		if (0 != ret)
			break;
		pr_debug(" %s : ret:%d Minor_number : %d\n", __func__,
							ret, tee_device.minor);
		tee_kobj = kobject_create_and_add("tee", kernel_kobj);
		if (!tee_kobj) {
			ret = -ENOMEM;
			misc_deregister(&tee_device);
			break;
		}
		ret = sysfs_create_group(tee_kobj, &tee_attribute_group);
		if (ret) {
			kobject_put(tee_kobj);
			misc_deregister(&tee_device);
			break;
		}
		ret = psp_comm_register_client(TEE_CLIENT_TYPE,
						&tee_callbackfunc);
		if (ret) {
			pr_err(" %s : Error register client", __func__);
			sysfs_remove_group(tee_kobj, &tee_attribute_group);
			kobject_put(tee_kobj);
			misc_deregister(&tee_device);
			break;
		}
		sema_init(&tee_drv_data.tee_sem, 0x1);
	} while (FALSE);
	pr_info(" %s : tee driver initialized ret : %d\n", __func__, ret);
	return (int)ret;
}

static void __exit tee_exit(
		void
		)
{
	sysfs_remove_group(tee_kobj, &tee_attribute_group);
	kobject_put(tee_kobj);
	misc_deregister(&tee_device);
	psp_comm_unregister_client(TEE_CLIENT_TYPE);
}

late_initcall(tee_init);
module_exit(tee_exit);

MODULE_AUTHOR("Advanced Micro Devices, Inc.");
MODULE_DESCRIPTION("tee driver");
