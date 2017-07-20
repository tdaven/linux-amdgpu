/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 */

#include <linux/module.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <drm/drmP.h>
#include <linux/dma-buf.h>
#include "amdgpu_amdkfd.h"
#include "amdgpu_ucode.h"
#include "gca/gfx_8_0_sh_mask.h"
#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_enum.h"
#include "oss/oss_3_0_sh_mask.h"
#include "oss/oss_3_0_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "vi_structs.h"
#include "vid.h"

/* Special VM and GART address alignment needed for VI pre-Fiji due to
 * a HW bug. */
#define VI_BO_SIZE_ALIGN (0x8000)

/* Impose limit on how much memory KFD can use */
struct kfd_mem_usage_limit {
	uint64_t max_system_mem_limit;
	uint64_t max_userptr_mem_limit;
	int64_t system_mem_used;
	int64_t userptr_mem_used;
	spinlock_t mem_limit_lock;
};

static struct kfd_mem_usage_limit kfd_mem_limit;


static inline struct amdgpu_device *get_amdgpu_device(struct kgd_dev *kgd)
{
	return (struct amdgpu_device *)kgd;
}

static bool check_if_add_bo_to_vm(struct amdgpu_vm *avm,
		struct kgd_mem *mem)
{
	struct kfd_bo_va_list *entry;

	list_for_each_entry(entry, &mem->bo_va_list, bo_list)
		if (entry->bo_va->vm == avm)
			return false;

	return true;
}

/* Set memory usage limits. Current, limits are
 *  System (kernel) memory - 1/4th System RAM
 *  Userptr memory - 3/4th System RAM
 */
void amdgpu_amdkfd_gpuvm_init_mem_limits(void)
{
	struct sysinfo si;
	uint64_t mem;

	si_meminfo(&si);
	mem = si.totalram - si.totalhigh;
	mem *= si.mem_unit;

	spin_lock_init(&kfd_mem_limit.mem_limit_lock);
	kfd_mem_limit.max_system_mem_limit = (mem >> 2) - (mem >> 4);
	kfd_mem_limit.max_userptr_mem_limit = mem - (mem >> 4);
	pr_debug("Kernel memory limit %lluM, userptr limit %lluM\n",
		(kfd_mem_limit.max_system_mem_limit >> 20),
		(kfd_mem_limit.max_userptr_mem_limit >> 20));
}

static int check_and_reserve_system_mem_limit(struct amdgpu_device *adev,
					      uint64_t size, u32 domain)
{
	size_t acc_size;
	int ret = 0;

	acc_size = ttm_bo_dma_acc_size(&adev->mman.bdev, size,
				       sizeof(struct amdgpu_bo));

	spin_lock(&kfd_mem_limit.mem_limit_lock);
	if (domain == AMDGPU_GEM_DOMAIN_GTT) {
		if (kfd_mem_limit.system_mem_used + (acc_size + size) >
			kfd_mem_limit.max_system_mem_limit) {
			ret = -ENOMEM;
			goto err_no_mem;
		}
		kfd_mem_limit.system_mem_used += (acc_size + size);

	} else if (domain == AMDGPU_GEM_DOMAIN_CPU) {
		if ((kfd_mem_limit.system_mem_used + acc_size >
			kfd_mem_limit.max_system_mem_limit) &&
			(kfd_mem_limit.userptr_mem_used + (size + acc_size) >
			kfd_mem_limit.max_userptr_mem_limit)) {
			ret = -ENOMEM;
			goto err_no_mem;
		}
		kfd_mem_limit.system_mem_used += acc_size;
		kfd_mem_limit.userptr_mem_used += size;
	}

err_no_mem:
	spin_unlock(&kfd_mem_limit.mem_limit_lock);
	return ret;
}

static void unreserve_system_memory_limit(struct amdgpu_bo *bo)
{
	spin_lock(&kfd_mem_limit.mem_limit_lock);

	if (bo->prefered_domains == AMDGPU_GEM_DOMAIN_GTT)
		kfd_mem_limit.system_mem_used -=
			(bo->tbo.acc_size + amdgpu_bo_size(bo));
	else if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		kfd_mem_limit.system_mem_used -= bo->tbo.acc_size;
		kfd_mem_limit.userptr_mem_used -= amdgpu_bo_size(bo);
	}
	if (kfd_mem_limit.system_mem_used < 0) {
		pr_warn("kfd system memory size ref. error\n");
		kfd_mem_limit.system_mem_used = 0;
	}
	if (kfd_mem_limit.userptr_mem_used < 0) {
		pr_warn("kfd userptr memory size ref. error\n");
		kfd_mem_limit.userptr_mem_used = 0;
	}

	spin_unlock(&kfd_mem_limit.mem_limit_lock);
}

static int add_bo_to_vm(struct amdgpu_device *adev, struct kgd_mem *mem,
		struct amdgpu_vm *avm, bool is_aql,
		struct kfd_bo_va_list **p_bo_va_entry)
{
	int ret;
	struct kfd_bo_va_list *bo_va_entry;
	uint32_t flags;
	struct amdgpu_bo *bo = mem->bo;
	uint64_t va = mem->va;
	struct list_head *list_bo_va = &mem->bo_va_list;
	bool readonly = mem->readonly;
	bool execute = mem->execute;

	if (is_aql)
		va += bo->tbo.mem.size;

	bo_va_entry = kzalloc(sizeof(*bo_va_entry), GFP_KERNEL);
	if (!bo_va_entry)
		return -ENOMEM;

	BUG_ON(va == 0);

	pr_debug("amdkfd: adding bo_va to bo %p and va 0x%llx id 0x%x\n",
			bo, va, adev->dev->id);

	/* Add BO to VM internal data structures*/
	bo_va_entry->bo_va = amdgpu_vm_bo_add(adev, avm, bo);
	if (bo_va_entry->bo_va == NULL) {
		ret = -EINVAL;
		pr_err("amdkfd: Failed to add BO object to VM. ret == %d\n",
				ret);
		goto err_vmadd;
	}

	flags = AMDGPU_PTE_READABLE | AMDGPU_PTE_WRITEABLE;
	if (readonly)
		flags = AMDGPU_PTE_READABLE;
	if (execute)
		flags |= AMDGPU_PTE_EXECUTABLE;

	/* Set virtual address for the allocation, allocate PTs,
	 * if needed, and zero them */
	ret = amdgpu_vm_bo_map(adev, bo_va_entry->bo_va,
			va, 0, amdgpu_bo_size(bo),
			flags | AMDGPU_PTE_VALID);
	if (ret != 0) {
		pr_err("amdkfd: Failed to set virtual address for BO. ret == %d (0x%llx)\n",
				ret, va);
		goto err_vmsetaddr;
	}

	bo_va_entry->kgd_dev = (void *)adev;
	bo_va_entry->is_mapped = false;
	list_add(&bo_va_entry->bo_list, list_bo_va);

	if (p_bo_va_entry)
		*p_bo_va_entry = bo_va_entry;

	return 0;

err_vmsetaddr:
	amdgpu_vm_bo_rmv(adev, bo_va_entry->bo_va);
	/* This will put the bo_va_mapping on the vm->freed
	 * list. amdgpu_vm_clear_freed needs the PTs to be reserved so
	 * we don't call it here. That can wait until the next time
	 * the page tables are updated for a map or unmap. */
err_vmadd:
	kfree(bo_va_entry);
	return ret;
}

static void remove_bo_from_vm(struct amdgpu_device *adev,
		struct kfd_bo_va_list *entry)
{
	amdgpu_vm_bo_rmv(adev, entry->bo_va);
	list_del(&entry->bo_list);
	kfree(entry);
}

static int amdgpu_amdkfd_bo_validate(struct amdgpu_bo *bo, uint32_t domain,
				     bool wait)
{
	int ret = 0;

	if (!amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		amdgpu_ttm_placement_from_domain(bo, domain);
		ret = ttm_bo_validate(&bo->tbo, &bo->placement,
				      false, false);
		if (ret)
			goto validate_fail;
		if (wait)
			ret = ttm_bo_wait(&bo->tbo, false, false);
	} else {
		/* Userptrs are not pinned. Therefore we can use the
		 * bo->pin_count for our version of pinning without conflict.
		 */
		if (bo->pin_count == 0) {
			amdgpu_ttm_placement_from_domain(bo, domain);
			ret = ttm_bo_validate(&bo->tbo, &bo->placement,
					      true, false);
			if (ret)
				goto validate_fail;
			if (wait)
				ttm_bo_wait(&bo->tbo, false, false);
		}
		bo->pin_count++;
	}

validate_fail:
	return ret;
}

static int amdgpu_amdkfd_bo_invalidate(struct amdgpu_bo *bo)
{
	int ret = 0;

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm) &&
		(--bo->pin_count == 0)) {
		amdgpu_ttm_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_CPU);
		ret = ttm_bo_validate(&bo->tbo, &bo->placement, true, false);
		if (ret != 0)
			pr_err("amdgpu: failed to invalidate userptr BO\n");
	}
	return ret;
}

static int validate_pt_pd_bos(struct amdgpu_vm *vm)
{
	int i, ret = 0;
	struct amdgpu_bo *bo, *pd = vm->page_directory;
	struct amdkfd_vm *kvm = container_of(vm, struct amdkfd_vm, base);

	/* Remove eviction fence so that validate can wait on move fences */
	amdgpu_bo_fence(pd, NULL, false);

	/* PTs share same reservation object as PD. So only fence PD */
	for (i = 0; i <= vm->max_pde_used; ++i) {
		bo = vm->page_tables[i].entry.robj;

		if (!bo)
			continue;

		ret = amdgpu_amdkfd_bo_validate(bo, AMDGPU_GEM_DOMAIN_VRAM,
						true);
		if (ret != 0) {
			pr_err("amdgpu: failed to validate PTE %d\n", i);
			break;
		}
	}

	ret = amdgpu_amdkfd_bo_validate(pd, AMDGPU_GEM_DOMAIN_VRAM,
					true);
	if (ret != 0) {
		pr_err("amdgpu: failed to validate PD\n");
		return ret;
	}

	/* Add the eviction fence back */
	amdgpu_bo_fence(pd, &kvm->master->eviction_fence->base, true);

	return ret;
}

/**
 * amdgpu_vm_clear_bo - initially clear the VRAM pages
 *
 * @adev: amdgpu_device pointer
 * @bo: bo to clear
 * @vm: requested vm
 * need to reserve bo first before calling it.
 */
static int amdgpu_amdkfd_gpuvm_clear_bo(struct amdgpu_device *adev,
			      struct amdgpu_vm *vm,
			      struct amdgpu_bo *bo)
{
	struct amdgpu_ring *ring;
	struct fence *fence = NULL;
	struct amdgpu_job *job;
	unsigned entries;
	uint64_t addr;
	int r;

	ring = container_of(vm->entity.sched, struct amdgpu_ring, sched);

	r = reservation_object_reserve_shared(bo->tbo.resv);
	if (r)
		return r;

	r = ttm_bo_validate(&bo->tbo, &bo->placement, true, false);
	if (r)
		goto error;

	addr = amdgpu_bo_gpu_offset(bo);
	entries = amdgpu_bo_size(bo);

	r = amdgpu_job_alloc_with_ib(adev, 64, &job);
	if (r)
		goto error;

	amdgpu_emit_fill_buffer(adev, &job->ibs[0], 0, addr, entries);
	amdgpu_ring_pad_ib(ring, &job->ibs[0]);

	WARN_ON(job->ibs[0].length_dw > 64);
	r = amdgpu_job_submit(job, ring, &vm->entity,
			      AMDGPU_FENCE_OWNER_VM, &fence);
	if (r)
		goto error_free;

	amdgpu_bo_fence(bo, fence, true);
	fence_put(fence);
	return 0;

error_free:
	amdgpu_job_free(job);

error:
	return r;
}

static void add_kgd_mem_to_kfd_bo_list(struct kgd_mem *mem,
				       struct amdkfd_vm *kfd_vm)
{
	struct amdgpu_bo_list_entry *entry = &mem->bo_list_entry;
	struct amdgpu_bo *bo = mem->bo;

	entry->robj = bo;
	INIT_LIST_HEAD(&entry->tv.head);
	entry->tv.shared = true;
	entry->tv.bo = &bo->tbo;
	mutex_lock(&kfd_vm->master->lock);
	list_add_tail(&entry->tv.head, &kfd_vm->master->kfd_bo_list);
	mutex_unlock(&kfd_vm->master->lock);
}

static int __alloc_memory_of_gpu(struct kgd_dev *kgd, uint64_t va,
		uint64_t size, void *vm, struct kgd_mem **mem,
		uint64_t *offset, void **kptr,
		u32 domain, u64 flags, struct sg_table *sg, bool aql_queue,
		bool readonly, bool execute, bool no_sub, bool userptr)
{
	struct amdgpu_device *adev;
	int ret;
	struct amdgpu_bo *bo;
	uint64_t user_addr = 0;
	int byte_align;
	u32 alloc_domain;
	struct amdkfd_vm *kfd_vm = (struct amdkfd_vm *)vm;

	BUG_ON(kgd == NULL);
	BUG_ON(size == 0);
	BUG_ON(mem == NULL);
	BUG_ON(kfd_vm == NULL);

	if (aql_queue)
		size = size >> 1;
	if (userptr) {
		if (!offset || !*offset)
			return -EINVAL;
		user_addr = *offset;
	}

	adev = get_amdgpu_device(kgd);
	byte_align = (adev->family == AMDGPU_FAMILY_VI &&
			adev->asic_type != CHIP_FIJI &&
			adev->asic_type != CHIP_POLARIS10 &&
			adev->asic_type != CHIP_POLARIS11) ?
			VI_BO_SIZE_ALIGN : 1;

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (*mem == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	INIT_LIST_HEAD(&(*mem)->bo_va_list);
	mutex_init(&(*mem)->lock);
	(*mem)->readonly = readonly;
	(*mem)->execute = execute;
	(*mem)->no_substitute = no_sub;
	(*mem)->aql_queue = aql_queue;

	alloc_domain = userptr ? AMDGPU_GEM_DOMAIN_CPU : domain;
	pr_debug("amdkfd: allocating BO on domain %d with size %llu\n",
				alloc_domain, size);

	ret = check_and_reserve_system_mem_limit(adev, size, alloc_domain);
	if (ret) {
		pr_err("amdkfd: Insufficient system memory\n");
		goto err_bo_create;
	}


	/* Allocate buffer object. Userptr objects need to start out
	 * in the CPU domain, get moved to GTT when pinned. */
	ret = amdgpu_bo_create(adev, size, byte_align, false,
				alloc_domain,
			       flags, sg, NULL, &bo);
	if (ret != 0) {
		pr_err("amdkfd: failed to create BO on domain %d. ret %d\n",
				alloc_domain, ret);
		goto err_bo_create;
	}
	bo->kfd_bo = *mem;
	(*mem)->bo = bo;

	if (domain == AMDGPU_GEM_DOMAIN_VRAM) {
		ret = amdgpu_bo_reserve(bo, true);
		if (ret) {
			dev_err(adev->dev,
				"(%d) failed to reserve bo for amdkfd\n", ret);
			goto err_bo_clear;
		}
		ret = amdgpu_amdkfd_gpuvm_clear_bo(adev, vm, bo);
		amdgpu_bo_unreserve(bo);
		if (ret) {
			pr_err("amdkfd: failed to clear VRAM BO object. ret %d\n",
					ret);
			goto err_bo_clear;
		}
	}

	pr_debug("amdkfd: created BO on domain %d with size %llu\n",
				alloc_domain, size);

	if (userptr) {
		ret = amdgpu_ttm_tt_set_userptr(bo->tbo.ttm, user_addr, 0);
		if (ret) {
			dev_err(adev->dev,
				"(%d) failed to set userptr\n", ret);
			goto allocate_mem_set_userptr_failed;
		}

		ret = amdgpu_mn_register(bo, user_addr);
		if (ret) {
			dev_err(adev->dev,
				"(%d) failed to register MMU notifier\n", ret);
			goto allocate_mem_set_userptr_failed;
		}
	}

	if (kptr) {
		ret = amdgpu_bo_reserve(bo, true);
		if (ret) {
			dev_err(adev->dev, "(%d) failed to reserve bo for amdkfd\n", ret);
			goto allocate_mem_reserve_bo_failed;
		}

		ret = amdgpu_bo_pin(bo, domain,
					NULL);
		if (ret) {
			dev_err(adev->dev, "(%d) failed to pin bo for amdkfd\n", ret);
			goto allocate_mem_pin_bo_failed;
		}

		ret = amdgpu_bo_kmap(bo, kptr);
		if (ret) {
			dev_err(adev->dev,
				"(%d) failed to map bo to kernel for amdkfd\n", ret);
			goto allocate_mem_kmap_bo_failed;
		}
		(*mem)->kptr = *kptr;

		amdgpu_bo_unreserve(bo);
	}

	(*mem)->va = va;
	(*mem)->domain = domain;
	(*mem)->mapped_to_gpu_memory = 0;
	add_kgd_mem_to_kfd_bo_list(*mem, kfd_vm);

	if (offset)
		*offset = amdgpu_bo_mmap_offset(bo);

	return 0;

allocate_mem_kmap_bo_failed:
	amdgpu_bo_unpin(bo);
allocate_mem_pin_bo_failed:
	amdgpu_bo_unreserve(bo);
allocate_mem_reserve_bo_failed:
	if (userptr)
		amdgpu_mn_unregister(bo);
allocate_mem_set_userptr_failed:
err_bo_clear:
	amdgpu_bo_unref(&bo);
err_bo_create:
	kfree(*mem);
err:
	return ret;
}

/* Reserving a BO and its page table BOs must happen atomically to
 * avoid deadlocks. When updating userptrs we need to temporarily
 * back-off the reservation and then reacquire it. Track all the
 * reservation info in a context structure. Buffers can be mapped to
 * multiple VMs simultaneously (buffers being restored on multiple
 * GPUs). */
struct bo_vm_reservation_context {
	struct amdgpu_bo_list_entry kfd_bo;
	unsigned n_vms;
	struct amdgpu_bo_list_entry *vm_pd;
	struct ww_acquire_ctx ticket;
	struct list_head list, duplicates;
	struct amdgpu_sync sync;
	bool reserved;
};

/**
 * reserve_bo_and_vm - reserve a BO and a VM unconditionally.
 * @mem: KFD BO structure.
 * @vm: the VM to reserve.
 * @ctx: the struct that will be used in unreserve_bo_and_vms().
 */
static int reserve_bo_and_vm(struct kgd_mem *mem,
			      struct amdgpu_vm *vm,
			      struct bo_vm_reservation_context *ctx)
{
	struct amdgpu_bo *bo = mem->bo;
	int ret;

	WARN_ON(!vm);

	ctx->reserved = false;
	ctx->n_vms = 1;
	amdgpu_sync_create(&ctx->sync);

	INIT_LIST_HEAD(&ctx->list);
	INIT_LIST_HEAD(&ctx->duplicates);

	ctx->vm_pd = kzalloc(sizeof(struct amdgpu_bo_list_entry)
			      * ctx->n_vms, GFP_KERNEL);
	if (ctx->vm_pd == NULL)
		return -ENOMEM;

	ctx->kfd_bo.robj = bo;
	ctx->kfd_bo.priority = 0;
	ctx->kfd_bo.tv.bo = &bo->tbo;
	ctx->kfd_bo.tv.shared = true;
	ctx->kfd_bo.user_pages = NULL;
	list_add(&ctx->kfd_bo.tv.head, &ctx->list);

	amdgpu_vm_get_pd_bo(vm, &ctx->list, &ctx->vm_pd[0]);
	amdgpu_vm_get_pt_bos(bo->adev, vm, &ctx->duplicates);

	ret = ttm_eu_reserve_buffers(&ctx->ticket, &ctx->list,
				     false, &ctx->duplicates);
	if (!ret)
		ctx->reserved = true;
	else
		pr_err("amdkfd: Failed to reserve buffers in ttm\n");

	if (ret) {
		kfree(ctx->vm_pd);
		ctx->vm_pd = NULL;
	}

	return ret;
}

enum VA_TYPE {
	VA_NOT_MAPPED = 0,
	VA_MAPPED,
	VA_DO_NOT_CARE,
};

/**
 * reserve_bo_and_vm - reserve a BO and some VMs that the BO has been added
 * to, conditionally based on map_type.
 * @mem: KFD BO structure.
 * @vm: the VM to reserve. If NULL, then all VMs associated with the BO
 * is used. Otherwise, a single VM associated with the BO.
 * @map_type: the mapping status that will be used to filter the VMs.
 * @ctx: the struct that will be used in unreserve_bo_and_vms().
 */
static int reserve_bo_and_cond_vms(struct kgd_mem *mem,
			      struct amdgpu_vm *vm, enum VA_TYPE map_type,
			      struct bo_vm_reservation_context *ctx)
{
	struct amdgpu_bo *bo = mem->bo;
	struct kfd_bo_va_list *entry;
	unsigned i;
	int ret;

	ctx->reserved = false;
	ctx->n_vms = 0;
	ctx->vm_pd = NULL;
	amdgpu_sync_create(&ctx->sync);

	INIT_LIST_HEAD(&ctx->list);
	INIT_LIST_HEAD(&ctx->duplicates);

	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if ((vm && vm != entry->bo_va->vm) ||
			(entry->is_mapped != map_type
			&& map_type != VA_DO_NOT_CARE))
			continue;

		ctx->n_vms++;
	}

	if (ctx->n_vms == 0)
		return 0;

	ctx->vm_pd = kzalloc(sizeof(struct amdgpu_bo_list_entry)
			      * ctx->n_vms, GFP_KERNEL);
	if (ctx->vm_pd == NULL)
		return -ENOMEM;

	ctx->kfd_bo.robj = bo;
	ctx->kfd_bo.priority = 0;
	ctx->kfd_bo.tv.bo = &bo->tbo;
	ctx->kfd_bo.tv.shared = true;
	ctx->kfd_bo.user_pages = NULL;
	list_add(&ctx->kfd_bo.tv.head, &ctx->list);

	i = 0;
	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if ((vm && vm != entry->bo_va->vm) ||
			(entry->is_mapped != map_type
			&& map_type != VA_DO_NOT_CARE))
			continue;

		amdgpu_vm_get_pd_bo(entry->bo_va->vm, &ctx->list,
				&ctx->vm_pd[i]);
		amdgpu_vm_get_pt_bos(bo->adev, entry->bo_va->vm,
				&ctx->duplicates);
		i++;
	}

	ret = ttm_eu_reserve_buffers(&ctx->ticket, &ctx->list,
				     false, &ctx->duplicates);
	if (!ret)
		ctx->reserved = true;
	else
		pr_err("amdkfd: Failed to reserve buffers in ttm\n");

	if (ret) {
		kfree(ctx->vm_pd);
		ctx->vm_pd = NULL;
	}

	return ret;
}

static void unreserve_bo_and_vms(struct bo_vm_reservation_context *ctx,
				 bool wait)
{
	if (wait)
		amdgpu_sync_wait(&ctx->sync);

	if (ctx->reserved)
		ttm_eu_backoff_reservation(&ctx->ticket, &ctx->list);
	if (ctx->vm_pd) {
		kfree(ctx->vm_pd);
	}
	amdgpu_sync_free(&ctx->sync);
	ctx->reserved = false;
	ctx->vm_pd = NULL;
}

/* Must be called with mem->lock held and a BO/VM reservation
 * context. Temporarily drops the lock and reservation for updating
 * user pointers, to avoid circular lock dependencies between MM locks
 * and buffer reservations. If user pages are invalidated while the
 * lock and reservation are dropped, try again. */
static int update_user_pages(struct kgd_mem *mem, struct mm_struct *mm,
			     struct bo_vm_reservation_context *ctx)
{
	struct amdgpu_bo *bo;
	unsigned tries = 10;
	int ret;

	bo = mem->bo;
	if (!amdgpu_ttm_tt_get_usermm(bo->tbo.ttm))
		return 0;

	if (bo->tbo.ttm->state != tt_bound) {
		struct page **pages;
		int invalidated;

		/* get user pages without locking the BO to avoid
		 * circular lock dependency with MMU notifier. Retry
		 * until we have the current version. */
		ttm_eu_backoff_reservation(&ctx->ticket, &ctx->list);
		ctx->reserved = false;
		pages = drm_calloc_large(bo->tbo.ttm->num_pages,
					 sizeof(struct page *));
		if (!pages)
			return -ENOMEM;

		mutex_unlock(&mem->lock);

		while (true) {
			down_read(&mm->mmap_sem);
			ret = amdgpu_ttm_tt_get_user_pages(bo->tbo.ttm, pages);
			up_read(&mm->mmap_sem);

			mutex_lock(&mem->lock);
			if (ret != 0)
				return ret;

			BUG_ON(bo != mem->bo);

			ret = ttm_eu_reserve_buffers(&ctx->ticket, &ctx->list,
						     false, &ctx->duplicates);
			if (unlikely(ret != 0)) {
				release_pages(pages, bo->tbo.ttm->num_pages, 0);
				drm_free_large(pages);
				return ret;
			}
			ctx->reserved = true;
			if (!amdgpu_ttm_tt_userptr_invalidated(bo->tbo.ttm,
							       &invalidated) ||
			    bo->tbo.ttm->state == tt_bound ||
			    --tries == 0)
				break;

			release_pages(pages, bo->tbo.ttm->num_pages, 0);
			ttm_eu_backoff_reservation(&ctx->ticket, &ctx->list);
			ctx->reserved = false;
			mutex_unlock(&mem->lock);
		}

		/* If someone else already bound it, release our pages
		 * array, otherwise copy it into the ttm BO. */
		if (bo->tbo.ttm->state == tt_bound || tries == 0)
			release_pages(pages, bo->tbo.ttm->num_pages, 0);
		else
			memcpy(bo->tbo.ttm->pages, pages,
			       sizeof(struct page *) * bo->tbo.ttm->num_pages);
		drm_free_large(pages);
	}

	if (tries == 0) {
		pr_err("Gave up trying to update user pages\n");
		return -EDEADLK;
	}

	return 0;
}

static int map_bo_to_gpuvm(struct amdgpu_device *adev, struct amdgpu_bo *bo,
		struct amdgpu_bo_va *bo_va,
		struct amdgpu_sync *sync)
{
	struct amdgpu_vm *vm;
	int ret;

	vm = bo_va->vm;
	/* Validate PT / PTs */
	ret = validate_pt_pd_bos(vm);
	if (ret != 0) {
		pr_err("amdkfd: Failed to validate PTs\n");
		goto err_unpin_bo;
	}

	/* Update the page directory */
	ret = amdgpu_vm_update_page_directory(adev, vm);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_update_page_directory\n");
		goto err_unpin_bo;
	}

	amdgpu_sync_fence(adev, sync, vm->page_directory_fence);

	/*
	 * The previously "released" BOs are really released and their VAs are
	 * removed from PT. This function is called here because it requires
	 * the radeon_vm::mutex to be locked and PT to be reserved
	 */
	ret = amdgpu_vm_clear_freed(adev, vm);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_clear_freed\n");
		goto err_unpin_bo;
	}

	/* Update the page tables  */
	ret = amdgpu_vm_bo_update(adev, bo_va, &bo->tbo.mem);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_bo_update\n");
		goto err_unpin_bo;
	}

	amdgpu_sync_fence(adev, sync, bo_va->last_pt_update);

	ret = amdgpu_vm_clear_invalids(adev, vm, sync);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_clear_invalids\n");
		goto err_failed_to_vm_clear_invalids;
	}

	/* Remove PTs from LRU list (reservation removed PD only) */
	amdgpu_vm_move_pt_bos_in_lru(adev, vm);

	return 0;

err_failed_to_vm_clear_invalids:
	amdgpu_vm_bo_update(adev, bo_va, NULL);
	amdgpu_sync_fence(adev, sync, bo_va->last_pt_update);
err_unpin_bo:
	amdgpu_amdkfd_bo_invalidate(bo);
	return ret;
}

static struct sg_table *create_doorbell_sg(uint64_t addr, uint32_t size)
{
	struct sg_table *sg = kmalloc(sizeof(struct sg_table), GFP_KERNEL);

	if (!sg)
		return NULL;
	if (sg_alloc_table(sg, 1, GFP_KERNEL)) {
		kfree(sg);
		return NULL;
	}
	sg->sgl->dma_address = addr;
	sg->sgl->length = size;
#ifdef CONFIG_NEED_SG_DMA_LENGTH
	sg->sgl->dma_length = size;
#endif
	return sg;
}

#define BOOL_TO_STR(b)	(b == true) ? "true" : "false"

int amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(
		struct kgd_dev *kgd, uint64_t va, uint64_t size,
		void *vm, struct kgd_mem **mem,
		uint64_t *offset, void **kptr,
		uint32_t flags)
{
	bool aql_queue, public, readonly, execute, no_sub, userptr;
	u64 alloc_flag;
	uint32_t domain;
	uint64_t *temp_offset;
	struct sg_table *sg = NULL;

	if (!(flags & ALLOC_MEM_FLAGS_NONPAGED)) {
		pr_err("amdgpu: current hw doesn't support paged memory\n");
		return -EINVAL;
	}

	domain = 0;
	alloc_flag = 0;
	temp_offset = NULL;

	aql_queue = (flags & ALLOC_MEM_FLAGS_AQL_QUEUE_MEM) ? true : false;
	public    = (flags & ALLOC_MEM_FLAGS_PUBLIC) ? true : false;
	readonly  = (flags & ALLOC_MEM_FLAGS_READONLY) ? true : false;
	execute   = (flags & ALLOC_MEM_FLAGS_EXECUTE_ACCESS) ? true : false;
	no_sub    = (flags & ALLOC_MEM_FLAGS_NO_SUBSTITUTE) ? true : false;
	userptr   = (flags & ALLOC_MEM_FLAGS_USERPTR) ? true : false;

	if (userptr && kptr) {
		pr_err("amdgpu: userptr can't be mapped to kernel\n");
		return -EINVAL;
	}

	/*
	 * Check on which domain to allocate BO
	 */
	if (flags & ALLOC_MEM_FLAGS_VRAM) {
		domain = AMDGPU_GEM_DOMAIN_VRAM;
		alloc_flag = AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
		if (public) {
			alloc_flag = AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
			temp_offset = offset;
		}
	} else if (flags & (ALLOC_MEM_FLAGS_GTT | ALLOC_MEM_FLAGS_USERPTR)) {
		domain = AMDGPU_GEM_DOMAIN_GTT;
		alloc_flag = 0;
		temp_offset = offset;
	} else if (flags & ALLOC_MEM_FLAGS_DOORBELL) {
		domain = AMDGPU_GEM_DOMAIN_GTT;
		alloc_flag = 0;
		temp_offset = offset;
		if (size > UINT_MAX)
			return -EINVAL;
		sg = create_doorbell_sg(*offset, size);
		if (!sg)
			return -ENOMEM;
	}

	if (offset && !userptr)
		*offset = 0;

	pr_debug("amdgpu: allocating BO domain %d alloc_flag 0x%llu public %s readonly %s execute %s no substitue %s va 0x%llx\n",
			domain,
			alloc_flag,
			BOOL_TO_STR(public),
			BOOL_TO_STR(readonly),
			BOOL_TO_STR(execute),
			BOOL_TO_STR(no_sub),
			va);

	return __alloc_memory_of_gpu(kgd, va, size, vm, mem,
			temp_offset, kptr, domain,
			alloc_flag, sg,
			aql_queue, readonly, execute,
			no_sub, userptr);
}

int amdgpu_amdkfd_gpuvm_free_memory_of_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm)
{
	struct amdgpu_device *adev;
	struct kfd_bo_va_list *entry, *tmp;
	struct bo_vm_reservation_context ctx;
	int ret;
	struct amdgpu_bo_list_entry *bo_list_entry;
	struct amdkfd_vm *master_vm;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);
	BUG_ON(vm == NULL);

	adev = get_amdgpu_device(kgd);
	master_vm = ((struct amdkfd_vm *)vm)->master;
	BUG_ON(master_vm == NULL);

	mutex_lock(&mem->lock);

	if (mem->mapped_to_gpu_memory > 0) {
		pr_err("BO with size %lu bytes is mapped to GPU. Need to unmap it before release va 0x%llx\n",
			mem->bo->tbo.mem.size, mem->va);
		mutex_unlock(&mem->lock);
		return -EBUSY;
	}

	mutex_unlock(&mem->lock);
	/* lock is not needed after this, since mem is unused and will
	 * be freed anyway */

	amdgpu_mn_unregister(mem->bo);
	if (mem->work.work.func)
		cancel_delayed_work_sync(&mem->work);

	ret = reserve_bo_and_cond_vms(mem, NULL, VA_DO_NOT_CARE, &ctx);
	if (unlikely(ret != 0))
		return ret;

	/* The eviction fence should be removed by the last unmap.
	 * TODO: Log an error condition if the bo still has the eviction fence
	 * attached
	 */
	amdgpu_bo_fence(mem->bo, NULL, false);
	pr_debug("Releasing BO with VA 0x%llx, size %lu bytes\n",
					mem->va,
					mem->bo->tbo.mem.size);

	/* Remove from VM internal data structures */
	list_for_each_entry_safe(entry, tmp, &mem->bo_va_list, bo_list) {
		pr_debug("\t remove from amdgpu_bo_va %p\n", entry->bo_va);
		remove_bo_from_vm((struct amdgpu_device *)entry->kgd_dev,
				entry);
	}

	unreserve_bo_and_vms(&ctx, false);

	/* If the SG is not NULL, it's one we created for a doorbell
	 * BO. We need to free it.
	 */
	if (mem->bo->tbo.sg) {
		sg_free_table(mem->bo->tbo.sg);
		kfree(mem->bo->tbo.sg);
	}

	/* Free the BO*/
	unreserve_system_memory_limit(mem->bo);
	bo_list_entry = &mem->bo_list_entry;
	mutex_lock(&master_vm->lock);
	list_del(&bo_list_entry->tv.head);
	mutex_unlock(&master_vm->lock);

	amdgpu_bo_unref(&mem->bo);
	kfree(mem);

	return 0;
}

int amdgpu_amdkfd_gpuvm_map_memory_to_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm)
{
	struct amdgpu_device *adev;
	int ret;
	struct amdgpu_bo *bo;
	uint32_t domain;
	struct kfd_bo_va_list *entry;
	struct bo_vm_reservation_context ctx;
	struct kfd_bo_va_list *bo_va_entry = NULL;
	struct kfd_bo_va_list *bo_va_entry_aql = NULL;
	struct amdkfd_vm *kfd_vm = (struct amdkfd_vm *)vm;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	adev = get_amdgpu_device(kgd);

	mutex_lock(&mem->lock);

	bo = mem->bo;

	BUG_ON(bo == NULL);

	domain = mem->domain;

	pr_debug("amdgpu: try to map VA 0x%llx domain %d\n",
			mem->va, domain);

	ret = reserve_bo_and_vm(mem, vm, &ctx);
	if (unlikely(ret != 0))
		goto bo_reserve_failed;

	if (check_if_add_bo_to_vm((struct amdgpu_vm *)vm, mem)) {
		pr_debug("amdkfd: add new BO_VA to list 0x%llx\n",
				mem->va);
		ret = add_bo_to_vm(adev, mem, (struct amdgpu_vm *)vm, false,
				&bo_va_entry);
		if (ret != 0)
			goto add_bo_to_vm_failed;
		if (mem->aql_queue) {
			ret = add_bo_to_vm(adev, mem, (struct amdgpu_vm *)vm,
					true, &bo_va_entry_aql);
			if (ret != 0)
				goto add_bo_to_vm_failed_aql;
		}
	}

	if (!mem->evicted) {
		ret = update_user_pages(mem, current->mm, &ctx);
		if (ret != 0)
			goto update_user_pages_failed;
	}

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		ret = amdgpu_amdkfd_bo_validate(bo, domain, true);
		if (ret) {
			pr_debug("amdkfd: userptr: Validate failed\n");
			goto map_bo_to_gpuvm_failed;
		}
	} else if (mem->mapped_to_gpu_memory == 0) {
		/* Validate BO only once. The eviction fence gets added to BO
		 * the first time it is mapped. Validate will wait for all
		 * background evictions to complete.
		 */
		ret = amdgpu_amdkfd_bo_validate(bo, domain, true);
		if (ret) {
			pr_debug("amdkfd: Validate failed\n");
			goto map_bo_to_gpuvm_failed;
		}
	}


	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if (entry->bo_va->vm == vm && !entry->is_mapped) {
			if (mem->evicted) {
				/* If the BO is evicted, just mark the
				 * mapping as mapped and stop the GPU's
				 * queues until the BO is restored. */
				ret = kgd2kfd->quiesce_mm(adev->kfd,
							  current->mm);
				if (ret != 0)
					goto quiesce_failed;
				entry->is_mapped = true;
				mem->mapped_to_gpu_memory++;
				continue;
			}

			pr_debug("amdkfd: Trying to map VA 0x%llx to vm %p\n",
					mem->va, vm);

			ret = map_bo_to_gpuvm(adev, bo, entry->bo_va,
					      &ctx.sync);
			if (ret != 0) {
				pr_err("amdkfd: Failed to map radeon bo to gpuvm\n");
				goto map_bo_to_gpuvm_failed;
			}
			entry->is_mapped = true;
			mem->mapped_to_gpu_memory++;
				pr_debug("amdgpu: INC mapping count %d\n",
					mem->mapped_to_gpu_memory);
		}
	}

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm) == NULL)
		amdgpu_bo_fence(bo, &kfd_vm->master->eviction_fence->base,
				true);
	unreserve_bo_and_vms(&ctx, true);

	mutex_unlock(&mem->lock);
	return 0;

map_bo_to_gpuvm_failed:
quiesce_failed:
update_user_pages_failed:
	if (bo_va_entry_aql)
		remove_bo_from_vm(adev, bo_va_entry_aql);
add_bo_to_vm_failed_aql:
	if (bo_va_entry)
		remove_bo_from_vm(adev, bo_va_entry);
add_bo_to_vm_failed:
	unreserve_bo_and_vms(&ctx, false);
bo_reserve_failed:
	mutex_unlock(&mem->lock);
	return ret;
}

static u64 get_vm_pd_gpu_offset(void *vm)
{
	struct amdgpu_vm *avm = (struct amdgpu_vm *) vm;
	u64 offset;

	BUG_ON(avm == NULL);

	amdgpu_bo_reserve(avm->page_directory, false);

	offset = amdgpu_bo_gpu_offset(avm->page_directory);

	amdgpu_bo_unreserve(avm->page_directory);

	return offset;
}

int amdgpu_amdkfd_gpuvm_create_process_vm(struct kgd_dev *kgd, void **vm,
					  void *master_vm)
{
	int ret;
	struct amdkfd_vm *new_vm;
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	BUG_ON(kgd == NULL);
	BUG_ON(vm == NULL);

	new_vm = kzalloc(sizeof(struct amdkfd_vm), GFP_KERNEL);
	if (new_vm == NULL)
		return -ENOMEM;

	/* Initialize the VM context, allocate the page directory and zero it */
	ret = amdgpu_vm_init(adev, &new_vm->base);
	if (ret != 0) {
		pr_err("amdgpu: failed init vm ret %d\n", ret);
		/* Undo everything related to the new VM context */
		goto vm_init_fail;
	}
	new_vm->adev = adev;
	mutex_init(&new_vm->lock);
	INIT_LIST_HEAD(&new_vm->kfd_bo_list);
	INIT_LIST_HEAD(&new_vm->kfd_vm_list);

	if (master_vm == NULL) {
		new_vm->master = new_vm;
		new_vm->eviction_fence =
			amdgpu_amdkfd_fence_create(fence_context_alloc(1),
						   current->mm);
		if (new_vm->master->eviction_fence == NULL) {
			pr_err("Failed to create eviction fence\n");
			goto evict_fence_fail;
		}
	} else {
		new_vm->master = master_vm;
		list_add_tail(&new_vm->kfd_vm_list,
			      &((struct amdkfd_vm *)master_vm)->kfd_vm_list);
	}
	new_vm->master->n_vms++;
	*vm = (void *) new_vm;

	/*
	 * The previously "released" BOs are really released and their VAs are
	 * removed from PT. This function is called here because it requires
	 * the radeon_vm::mutex to be locked and PT to be reserved
	 */
	ret = amdgpu_vm_clear_freed(adev, &new_vm->base);
	if (ret != 0)
		pr_err("amdgpu: Failed to amdgpu_vm_clear_freed\n");

	pr_debug("amdgpu: created process vm with address 0x%llx\n",
			get_vm_pd_gpu_offset(&new_vm->base));

	return ret;

evict_fence_fail:
	amdgpu_vm_fini(adev, &new_vm->base);
vm_init_fail:
	kfree(new_vm);
	return ret;

}

void amdgpu_amdkfd_gpuvm_destroy_process_vm(struct kgd_dev *kgd, void *vm)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	struct amdkfd_vm *kfd_vm = (struct amdkfd_vm *) vm;
	struct amdgpu_vm *avm = &kfd_vm->base;
	struct amdgpu_bo *pd;

	BUG_ON(kgd == NULL);
	BUG_ON(vm == NULL);

	pr_debug("Destroying process vm with address %p\n", vm);
	/* Release eviction fence from PD */
	pd = avm->page_directory;
	amdgpu_bo_reserve(pd, false);
	amdgpu_bo_fence(pd, NULL, false);
	amdgpu_bo_unreserve(pd);

	/* Release eviction fence */
	if (kfd_vm->master == kfd_vm && kfd_vm->eviction_fence != NULL)
		fence_put(&kfd_vm->eviction_fence->base);

	/* Release the VM context */
	amdgpu_vm_fini(adev, avm);
	kfree(vm);
}

uint32_t amdgpu_amdkfd_gpuvm_get_process_page_dir(void *vm)
{
	return get_vm_pd_gpu_offset(vm) >> AMDGPU_GPU_PAGE_SHIFT;
}

int amdgpu_amdkfd_gpuvm_get_vm_fault_info(struct kgd_dev *kgd,
					      struct kfd_vm_fault_info *mem)
{
	struct amdgpu_device *adev;

	BUG_ON(kgd == NULL);
	adev = (struct amdgpu_device *) kgd;
	if (atomic_read(&adev->mc.vm_fault_info_updated) == 1) {
		*mem = *adev->mc.vm_fault_info;
		mb();
		atomic_set(&adev->mc.vm_fault_info_updated, 0);
	}
	return 0;
}

static int unmap_bo_from_gpuvm(struct amdgpu_device *adev,
				struct amdgpu_bo *bo,
				struct amdgpu_bo_va *bo_va,
				struct amdgpu_sync *sync)
{
	struct amdgpu_vm *vm = bo_va->vm;

	/*
	 * The previously "released" BOs are really released and their VAs are
	 * removed from PT. This function is called here because it requires
	 * the radeon_vm::mutex to be locked and PT to be reserved
	 */
	amdgpu_vm_clear_freed(adev, vm);

	/* Update the page tables - Remove the mapping from bo_va */
	amdgpu_vm_bo_update(adev, bo_va, NULL);
	amdgpu_sync_fence(adev, sync, bo_va->last_pt_update);

	amdgpu_vm_clear_invalids(adev, vm, sync);

	amdgpu_amdkfd_bo_invalidate(bo);

	return 0;
}

static bool is_mem_on_local_device(struct kgd_dev *kgd,
		struct list_head *bo_va_list, void *vm)
{
	struct kfd_bo_va_list *entry;

	list_for_each_entry(entry, bo_va_list, bo_list) {
		if (entry->kgd_dev == kgd && entry->bo_va->vm == vm)
			return true;
	}

	return false;
}

int amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm)
{
	struct kfd_bo_va_list *entry;
	struct amdgpu_device *adev;
	unsigned mapped_before;
	int ret = 0;
	struct bo_vm_reservation_context ctx;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	adev = (struct amdgpu_device *) kgd;

	mutex_lock(&mem->lock);

	/*
	 * Make sure that this BO mapped on KGD before unmappping it
	 */
	if (!is_mem_on_local_device(kgd, &mem->bo_va_list, vm)) {
		ret = -EINVAL;
		goto out;
	}

	if (mem->mapped_to_gpu_memory == 0) {
		pr_debug("BO size %lu bytes at va 0x%llx is not mapped\n",
			 mem->bo->tbo.mem.size, mem->va);
		ret = -EINVAL;
		goto out;
	}
	mapped_before = mem->mapped_to_gpu_memory;

	ret = reserve_bo_and_cond_vms(mem, vm, VA_MAPPED, &ctx);
	if (unlikely(ret != 0))
		goto out;

	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if (entry->bo_va->vm == vm && entry->is_mapped) {
			if (mem->evicted) {
				/* If the BO is evicted, just mark the
				 * mapping as unmapped and allow the
				 * GPU's queues to resume. */
				ret = kgd2kfd->resume_mm(adev->kfd,
							 current->mm);
				if (ret != 0)
					goto unreserve_out;
				entry->is_mapped = false;
				mem->mapped_to_gpu_memory--;
				continue;
			}

			pr_debug("unmapping BO with VA 0x%llx, size %lu bytes from GPU memory\n",
				mem->va,
				mem->bo->tbo.mem.size);

			ret = unmap_bo_from_gpuvm(adev, mem->bo,
						entry->bo_va, &ctx.sync);
			if (ret == 0) {
				entry->is_mapped = false;
			} else {
				pr_err("amdgpu: failed unmap va 0x%llx\n",
						mem->va);
				goto unreserve_out;
			}

			mem->mapped_to_gpu_memory--;
			pr_debug("amdgpu: DEC mapping count %d\n",
					mem->mapped_to_gpu_memory);
		}
	}

	/* If BO is unmapped from all VMs, unfence it. It can be evicted if
	 * required.
	 * TODO: For interop this will remove fences added by graphics driver.
	 * Remove only KFD eviction fence
	 */
	if (mem->mapped_to_gpu_memory == 0)
		amdgpu_bo_fence(mem->bo, NULL, false);

	if (mapped_before == mem->mapped_to_gpu_memory) {
		pr_debug("BO size %lu bytes at va 0x%llx is not mapped on GPU %x:%x.%x\n",
			 mem->bo->tbo.mem.size, mem->va,
			 adev->pdev->bus->number, PCI_SLOT(adev->pdev->devfn),
			 PCI_FUNC(adev->pdev->devfn));
		ret = -EINVAL;
	}

unreserve_out:
	unreserve_bo_and_vms(&ctx, false);
out:
	mutex_unlock(&mem->lock);
	return ret;
}

int amdgpu_amdkfd_gpuvm_mmap_bo(struct kgd_dev *kgd, struct vm_area_struct *vma)
{
	struct amdgpu_device *adev;

	adev = get_amdgpu_device(kgd);
	BUG_ON(!adev);

	return amdgpu_bo_mmap(NULL, vma, &adev->mman.bdev);
}

int amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel(struct kgd_dev *kgd,
		struct kgd_mem *mem, void **kptr)
{
	int ret;
	struct amdgpu_device *adev;
	struct amdgpu_bo *bo;

	adev = get_amdgpu_device(kgd);

	mutex_lock(&mem->lock);

	bo = mem->bo;
	/* map the buffer */
	ret = amdgpu_bo_reserve(bo, true);
	if (ret) {
		dev_err(adev->dev, "(%d) failed to reserve bo for amdkfd\n", ret);
		mutex_unlock(&mem->lock);
		return ret;
	}

	ret = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT,
			NULL);
	if (ret) {
		dev_err(adev->dev, "(%d) failed to pin bo for amdkfd\n", ret);
		amdgpu_bo_unreserve(bo);
		mutex_unlock(&mem->lock);
		return ret;
	}

	ret = amdgpu_bo_kmap(bo, kptr);
	if (ret) {
		dev_err(adev->dev,
			"(%d) failed to map bo to kernel for amdkfd\n", ret);
		amdgpu_bo_unpin(bo);
		amdgpu_bo_unreserve(bo);
		mutex_unlock(&mem->lock);
		return ret;
	}

	mem->kptr = *kptr;

	amdgpu_bo_unreserve(bo);
	mutex_unlock(&mem->lock);

	return 0;
}

static int pin_bo_wo_map(struct kgd_mem *mem)
{
	struct amdgpu_bo *bo = mem->bo;
	int ret = 0;

	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return ret;

	ret = amdgpu_bo_pin(bo, mem->domain, NULL);
	amdgpu_bo_unreserve(bo);

	return ret;
}

static void unpin_bo_wo_map(struct kgd_mem *mem)
{
	struct amdgpu_bo *bo = mem->bo;
	int ret = 0;

	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return;

	amdgpu_bo_unpin(bo);
	amdgpu_bo_unreserve(bo);
}

#define AMD_GPU_PAGE_SHIFT	PAGE_SHIFT
#define AMD_GPU_PAGE_SIZE (_AC(1, UL) << AMD_GPU_PAGE_SHIFT)

static int get_sg_table(struct amdgpu_device *adev,
		struct kgd_mem *mem, uint64_t offset,
		uint64_t size, struct sg_table **ret_sg)
{
	struct amdgpu_bo *bo = mem->bo;
	struct sg_table *sg = NULL;
	unsigned long bus_addr;
	unsigned int chunks;
	unsigned int i;
	struct scatterlist *s;
	uint64_t offset_in_page;
	unsigned int page_size;
	int ret;

	sg = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sg) {
		ret = -ENOMEM;
		goto out;
	}

	if (bo->prefered_domains == AMDGPU_GEM_DOMAIN_VRAM)
		page_size = AMD_GPU_PAGE_SIZE;
	else
		page_size = PAGE_SIZE;


	offset_in_page = offset & (page_size - 1);
	chunks = (size  + offset_in_page + page_size - 1)
			/ page_size;

	ret = sg_alloc_table(sg, chunks, GFP_KERNEL);
	if (unlikely(ret))
		goto out;

	if (bo->prefered_domains == AMDGPU_GEM_DOMAIN_VRAM) {
		bus_addr = bo->tbo.offset + adev->mc.aper_base + offset;

		for_each_sg(sg->sgl, s, sg->orig_nents, i) {
			uint64_t chunk_size, length;

			chunk_size = page_size - offset_in_page;
			length = min(size, chunk_size);

			sg_set_page(s, NULL, length, offset_in_page);
			s->dma_address = bus_addr;
			s->dma_length = length;

			size -= length;
			offset_in_page = 0;
			bus_addr += length;
		}
	} else {
		struct page **pages;
		unsigned int cur_page;

		pages = bo->tbo.ttm->pages;

		cur_page = offset / page_size;
		for_each_sg(sg->sgl, s, sg->orig_nents, i) {
			uint64_t chunk_size, length;

			chunk_size = page_size - offset_in_page;
			length = min(size, chunk_size);

			sg_set_page(s, pages[cur_page], length, offset_in_page);
			s->dma_address = page_to_phys(pages[cur_page]);
			s->dma_length = length;

			size -= length;
			offset_in_page = 0;
			cur_page++;
		}
	}

	*ret_sg = sg;
	return 0;
out:
	kfree(sg);
	*ret_sg = NULL;
	return ret;
}

int amdgpu_amdkfd_gpuvm_pin_get_sg_table(struct kgd_dev *kgd,
		struct kgd_mem *mem, uint64_t offset,
		uint64_t size, struct sg_table **ret_sg)
{
	int ret;
	struct amdgpu_device *adev;

	ret = pin_bo_wo_map(mem);
	if (unlikely(ret != 0))
		return ret;

	adev = get_amdgpu_device(kgd);

	ret = get_sg_table(adev, mem, offset, size, ret_sg);
	if (ret)
		unpin_bo_wo_map(mem);

	return ret;
}

void amdgpu_amdkfd_gpuvm_unpin_put_sg_table(
		struct kgd_mem *mem, struct sg_table *sg)
{
	sg_free_table(sg);
	kfree(sg);

	unpin_bo_wo_map(mem);
}

int amdgpu_amdkfd_gpuvm_import_dmabuf(struct kgd_dev *kgd, int dma_buf_fd,
				      uint64_t va, void *vm,
				      struct kgd_mem **mem, uint64_t *size)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;
	struct dma_buf *dma_buf;
	struct drm_gem_object *obj;
	struct amdgpu_bo *bo;
	int r = 0;
	struct amdkfd_vm *kfd_vm = (struct amdkfd_vm *)vm;

	dma_buf = dma_buf_get(dma_buf_fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	if (dma_buf->ops != &drm_gem_prime_dmabuf_ops)
		/* Can't handle non-graphics buffers */
		goto out_put;

	obj = dma_buf->priv;
	if (obj->dev->dev_private != adev)
		/* Can't handle buffers from other devices */
		goto out_put;

	bo = gem_to_amdgpu_bo(obj);
	if (!(bo->prefered_domains & (AMDGPU_GEM_DOMAIN_VRAM |
				    AMDGPU_GEM_DOMAIN_GTT)))
		/* Only VRAM and GTT BOs are supported */
		goto out_put;

	if (size)
		*size = amdgpu_bo_size(bo);

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (*mem == NULL) {
		r = -ENOMEM;
		goto out_put;
	}

	INIT_LIST_HEAD(&(*mem)->bo_va_list);
	mutex_init(&(*mem)->lock);
	(*mem)->readonly = false;
	(*mem)->execute = true; /* executable by default */

	(*mem)->bo = amdgpu_bo_ref(bo);
	(*mem)->va = va;
	(*mem)->domain = (bo->prefered_domains & AMDGPU_GEM_DOMAIN_VRAM) ?
		AMDGPU_GEM_DOMAIN_VRAM : AMDGPU_GEM_DOMAIN_GTT;
	(*mem)->mapped_to_gpu_memory = 0;
	add_kgd_mem_to_kfd_bo_list(*mem, kfd_vm);

out_put:
	dma_buf_put(dma_buf);
	return r;
}

/* Runs out of process context. mem->lock must be held. */
int amdgpu_amdkfd_gpuvm_evict_mem(struct kgd_mem *mem, struct mm_struct *mm)
{
	struct kfd_bo_va_list *entry;
	unsigned n_evicted;
	int r = 0;
	struct bo_vm_reservation_context ctx;

	pr_debug("Evicting buffer %p\n", mem);

	if (mem->mapped_to_gpu_memory == 0)
		return 0;

	/* Remove all GPU mappings of the buffer, but don't change any
	 * of the is_mapped flags so we can restore it later. The
	 * queues of the affected GPUs are quiesced first. Count the
	 * number of evicted mappings so we can roll back if something
	 * goes wrong. */
	n_evicted = 0;

	r = reserve_bo_and_cond_vms(mem, NULL, VA_MAPPED, &ctx);
	if (unlikely(r != 0))
		return r;

	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		struct amdgpu_device *adev;

		if (!entry->is_mapped)
			continue;

		adev = (struct amdgpu_device *)entry->kgd_dev;

		r = kgd2kfd->quiesce_mm(adev->kfd, mm);
		if (r != 0) {
			pr_err("failed to quiesce KFD\n");
			goto fail;
		}

		r = unmap_bo_from_gpuvm(adev, mem->bo,
					entry->bo_va, &ctx.sync);
		if (r != 0) {
			pr_err("failed unmap va 0x%llx\n",
			       mem->va);
			kgd2kfd->resume_mm(adev->kfd, mm);
			goto fail;
		}

		n_evicted++;
	}

	unreserve_bo_and_vms(&ctx, true);

	return 0;

fail:
	unreserve_bo_and_vms(&ctx, true);
	/* To avoid hangs and keep state consistent, roll back partial
	 * eviction by restoring queues and marking mappings as
	 * unmapped. Access to now unmapped buffers will fault. */
	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		struct amdgpu_device *adev;

		if (n_evicted == 0)
			break;
		if (!entry->is_mapped)
			continue;

		entry->is_mapped = false;

		adev = (struct amdgpu_device *)entry->kgd_dev;
		if (kgd2kfd->resume_mm(adev->kfd, mm))
			pr_err("Failed to resume KFD\n");

		n_evicted--;
	}

	return r;
}

/* Runs out of process context. mem->lock must be held. */
int amdgpu_amdkfd_gpuvm_restore_mem(struct kgd_mem *mem, struct mm_struct *mm)
{
	struct bo_vm_reservation_context ctx;
	struct kfd_bo_va_list *entry;
	uint32_t domain;
	int r, ret = 0;
	bool have_pages = false;

	pr_debug("Restoring buffer %p\n", mem);

	if (mem->mapped_to_gpu_memory == 0)
		return 0;

	domain = mem->domain;

	ret = reserve_bo_and_cond_vms(mem, NULL, VA_MAPPED, &ctx);
	if (likely(ret == 0)) {
		ret = update_user_pages(mem, mm, &ctx);
		have_pages = !ret;
		if (!have_pages)
			unreserve_bo_and_vms(&ctx, false);
	}

	/* update_user_pages drops the lock briefly. Check if someone
	 * else evicted or restored the buffer in the mean time */
	if (mem->evicted != 1) {
		unreserve_bo_and_vms(&ctx, false);
		return 0;
	}

	/* Try to restore all mappings. Mappings that fail to restore
	 * will be marked as unmapped. If we failed to get the user
	 * pages, all mappings will be marked as unmapped. */
	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		struct amdgpu_device *adev;

		if (!entry->is_mapped)
			continue;

		adev = (struct amdgpu_device *)entry->kgd_dev;

		if (unlikely(!have_pages)) {
			entry->is_mapped = false;
			continue;
		}

		r = amdgpu_amdkfd_bo_validate(mem->bo, domain, true);
		if (unlikely(r != 0)) {
			pr_err("Failed to validate BO\n");
			entry->is_mapped = false;
			if (ret == 0)
				ret = r;
			continue;
		}

		r = map_bo_to_gpuvm(adev, mem->bo, entry->bo_va,
				    &ctx.sync);
		if (unlikely(r != 0)) {
			pr_err("Failed to map BO to gpuvm\n");
			entry->is_mapped = false;
			if (ret == 0)
				ret = r;
		}
	}

	if (have_pages)
		unreserve_bo_and_vms(&ctx, true);

	/* Resume queues after unreserving the BOs and most
	 * importantly, waiting for the BO fences to guarantee that
	 * the page table updates have completed.
	 */
	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		struct amdgpu_device *adev;

		if (!entry->is_mapped)
			continue;

		adev = (struct amdgpu_device *)entry->kgd_dev;

		r = kgd2kfd->resume_mm(adev->kfd, mm);
		if (ret != 0) {
			pr_err("Failed to resume KFD\n");
			if (ret == 0)
				ret = r;
		}
	}

	return ret;
}

/** amdgpu_amdkfd_gpuvm_restore_process_bos - Restore all BOs for the given
 *   KFD process identified by master_vm
 *
 * @master_vm: Master VM of the KFD process
 *
 * After memory eviction, restore thread calls this function. The function
 * should be called when the Process is still valid. BO restore involves -
 *
 * 1.  Release old eviction fence and create new one
 * 2.  Get PD BO list and PT BO list from all the VMs.
 * 3.  Merge PD BO list into KFD BO list. Reserve all BOs.
 * 4.  Split the list into PD BO list and KFD BO list
 * 5.  Validate of PD and PT BOs and add new fence to the BOs
 * 6.  Validate all KFD BOs and Map them and add new fence
 * 7.  Merge PD BO list into KFD BO list. Unreserve all BOs
 * 8.  Restore back KFD BO list by removing PD BO entries
 */

int amdgpu_amdkfd_gpuvm_restore_process_bos(void *m_vm)
{
	struct amdgpu_bo_list_entry *pd_bo_list, *last_pd_bo_entry, *entry;
	struct amdkfd_vm *master_vm = (struct amdkfd_vm *)m_vm, *peer_vm;
	struct kgd_mem *mem;
	struct bo_vm_reservation_context ctx;
	struct amdgpu_amdkfd_fence *old_fence;
	int ret = 0, i;

	if (WARN_ON(master_vm == NULL || master_vm->master != master_vm))
		return -EINVAL;

	INIT_LIST_HEAD(&ctx.list);
	INIT_LIST_HEAD(&ctx.duplicates);

	pd_bo_list = kcalloc(master_vm->n_vms,
			     sizeof(struct amdgpu_bo_list_entry),
			     GFP_KERNEL);
	if (pd_bo_list == NULL)
		return -ENOMEM;

	/* Release old eviction fence and create new one. Use context and mm
	 * from the old fence.
	 */
	old_fence = master_vm->eviction_fence;
	master_vm->eviction_fence =
		amdgpu_amdkfd_fence_create(old_fence->base.context,
					   old_fence->mm);
	fence_put(&old_fence->base);
	if (master_vm->eviction_fence == NULL) {
		pr_err("Failed to create eviction fence\n");
		goto evict_fence_fail;
	}

	/* Get PD BO list and PT BO list from all the VMs */
	amdgpu_vm_get_pd_bo(&master_vm->base, &ctx.list,
			    &pd_bo_list[0]);
	amdgpu_vm_get_pt_bos(master_vm->adev, &master_vm->base,
			     &ctx.duplicates);

	i = 1;
	list_for_each_entry(peer_vm, &master_vm->kfd_vm_list, kfd_vm_list) {
		amdgpu_vm_get_pd_bo(&peer_vm->base, &ctx.list,
				    &pd_bo_list[i]);
		amdgpu_vm_get_pt_bos(peer_vm->adev, &peer_vm->base,
				     &ctx.duplicates);
		i++;
	}

	/* Needed to splicing and cutting the lists */
	last_pd_bo_entry = list_last_entry(&ctx.list,
					   struct amdgpu_bo_list_entry,
					   tv.head);

	/* Reserve all BOs and page tables/directory. */
	mutex_lock(&master_vm->lock);
	list_splice_init(&ctx.list, &master_vm->kfd_bo_list);
	ret = ttm_eu_reserve_buffers(&ctx.ticket, &master_vm->kfd_bo_list,
				     false, &ctx.duplicates);
	if (ret) {
		pr_debug("Memory eviction: TTM Reserve Failed. Try again\n");
		goto ttm_reserve_fail;
	}

	/* Restore kfd_bo_list. ctx.list contains only PDs */
	list_cut_position(&ctx.list, &master_vm->kfd_bo_list,
			  &last_pd_bo_entry->tv.head);

	amdgpu_sync_create(&ctx.sync);

	/* Validate PDs and PTs */
	list_for_each_entry(entry, &ctx.list, tv.head) {
		struct amdgpu_bo *bo = entry->robj;

		ret = amdgpu_amdkfd_bo_validate(bo, bo->prefered_domains,
						false);
		if (ret) {
			pr_debug("Memory eviction: Validate failed. Try again\n");
			goto validate_map_fail;
		}
	}
	list_for_each_entry(entry, &ctx.duplicates, tv.head) {
		struct amdgpu_bo *bo = entry->robj;

		ret = amdgpu_amdkfd_bo_validate(bo, bo->prefered_domains,
						false);
		if (ret) {
			pr_debug("Memory eviction: Validate failed. Try again\n");
			goto validate_map_fail;
		}
	}

	/* Wait for PT/PD validate to finish and attach eviction fence.
	 * PD/PT share the same reservation object
	 */
	list_for_each_entry(entry, &ctx.list, tv.head) {
		struct amdgpu_bo *bo = entry->robj;

		ttm_bo_wait(&bo->tbo, false, false);
	}


	/* Validate BOs and map them to GPUVM (update VM page tables). */
	list_for_each_entry(mem, &master_vm->kfd_bo_list,
			    bo_list_entry.tv.head) {

		struct amdgpu_bo *bo = mem->bo;
		uint32_t domain = mem->domain;
		struct kfd_bo_va_list *bo_va_entry;

		ret = amdgpu_amdkfd_bo_validate(bo, domain, false);
		if (ret) {
			pr_debug("Memory eviction: Validate failed. Try again\n");
			goto validate_map_fail;
		}

		list_for_each_entry(bo_va_entry, &mem->bo_va_list,
				    bo_list) {
			ret = map_bo_to_gpuvm((struct amdgpu_device *)
					      bo_va_entry->kgd_dev,
					      bo, bo_va_entry->bo_va,
					      &ctx.sync);
			if (ret) {
				pr_debug("Memory eviction: Map failed. Try again\n");
				goto validate_map_fail;
			}
		}
	}

	amdgpu_sync_wait(&ctx.sync);

	/* Wait for validate to finish and attach new eviction fence */
	list_for_each_entry(mem, &master_vm->kfd_bo_list,
		bo_list_entry.tv.head) {
		struct amdgpu_bo *bo = mem->bo;

		ttm_bo_wait(&bo->tbo, false, false);
		amdgpu_bo_fence(bo, &master_vm->eviction_fence->base, true);
	}
	list_for_each_entry(entry, &ctx.list, tv.head) {
		struct amdgpu_bo *bo = entry->robj;

		amdgpu_bo_fence(bo, &master_vm->eviction_fence->base, true);
	}
validate_map_fail:
	/* Add PDs to kfd_bo_list for unreserve */
	list_splice_init(&ctx.list, &master_vm->kfd_bo_list);
	ttm_eu_backoff_reservation(&ctx.ticket, &master_vm->kfd_bo_list);
	amdgpu_sync_free(&ctx.sync);
ttm_reserve_fail:
	/* Restore kfd_bo_list */
	list_cut_position(&ctx.list, &master_vm->kfd_bo_list,
			  &last_pd_bo_entry->tv.head);
	mutex_unlock(&master_vm->lock);
evict_fence_fail:
	kfree(pd_bo_list);
	return ret;
}
