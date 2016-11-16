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
#include <drm/drmP.h>
#include <drm/drm.h>
#include <asm/pgtable.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include "smu/smu_8_0_d.h"
#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "amdgpu_psp_if.h"

struct amdgpu_psp	*psp_data;

static void g2p_set_irq_funcs(struct amdgpu_device *adev);
static void g2p_print_status(void *);

/*
 * This is the .early_init entry from the GPU driver initialization sequence.
 * This function will be called from amdgpu_device_init()
 * Here we should do the following:
 *	(1) Perform early initialization like setting up function pointers.
 */
static int g2p_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dev_dbg(adev->dev, "%s called\n", __func__);
	psp_data = &adev->psp;
	psp_data->adev = adev;

	g2p_set_irq_funcs(adev);

	return 0;
}

/*
 * This is the .sw_init entry from the GPU driver initialization sequence.
 * This function will be called from amdgpu_init()
 * Here we should do the following software initialization:
 *		Initialize a mutex
 *		Initialize wait queue
 *		Allocate the Ring buffer
 */
static int g2p_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;
	u32 size;
	void *addr;

	dev_dbg(adev->dev, "%s called\n", __func__);

	/* PSP Interrupt MP0_SW_INT (252) */
	r = amdgpu_irq_add_id(adev, 252, &adev->psp.irq);
	if (r)
		return r;

	mutex_init(&adev->psp.psp_mutex);

	init_waitqueue_head(&adev->psp.psp_queue);

	size = sizeof(struct g2p_comm_rb_frame) * MAX_FRAMES;
	dev_info(adev->dev, "Ring Size = %d\n", size);
	/*
	 * Allocate memory for GPCOM ring with 128 frames.
	 * Memory needs to be physically contiguous.
	 */
	addr = (void *)__get_free_pages(GFP_KERNEL, get_order(size));
	if (!addr)
		return -ENOMEM;

	memset(addr, 0, PAGE_SIZE << get_order(size));
	adev->psp.virt_addr = (struct g2p_comm_rb_frame *)(addr);
	adev->psp.phys_addr = (struct g2p_comm_rb_frame *) virt_to_phys(addr);

	adev->psp.is_resp_recvd = 0;

	/* Allocate memory for fence */
	adev->psp.fence_addr = (void *)__get_free_pages(GFP_KERNEL,
							get_order(1024));
	if (!adev->psp.fence_addr) {
		free_pages((unsigned long)adev->psp.virt_addr,
							get_order(size));
		return -ENOMEM;
	}

	dev_dbg(adev->dev, "Ring buffer Virt addr = %p\n",
						adev->psp.virt_addr);
	dev_dbg(adev->dev, "Ring buffer Physical addr = %p\n",
						adev->psp.phys_addr);
	dev_dbg(adev->dev, "Fence Virt addr = %p\n", adev->psp.fence_addr);
	dev_dbg(adev->dev, "Fence Physical addr = %p\n",
				(u64 *)virt_to_phys(adev->psp.fence_addr));
	dev_info(adev->dev, "%s completed successfully\n", __func__);

	return 0;
}

/*
 * This is the .sw_fini entry from the GPU driver teardown sequence.
 * Here we should do the following software teardown:
 *		(1) Free the ring buffer.
 *		(2) Destroy Wait Queues
 *		(3) Release the mutex
 */
static int g2p_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 size;

	dev_dbg(adev->dev, "%s called\n", __func__);

	size = sizeof(struct g2p_comm_rb_frame) * MAX_FRAMES;

	/* Free the memory for the ring buffer */
	if (adev->psp.virt_addr)
		free_pages((unsigned long)adev->psp.virt_addr,
						get_order(size));

	/* Free memory allocated for fence */
	if (adev->psp.fence_addr)
		free_pages((unsigned long)adev->psp.fence_addr,
						get_order(1024));

	adev->psp.virt_addr = NULL;
	adev->psp.phys_addr = NULL;
	adev->psp.fence_addr = NULL;

	mutex_destroy(&adev->psp.psp_mutex);

	return 0;
}

/*
 * This is the .hw_init entry from the GPU driver initialization sequence.
 * This function will be called from amdgpu_init()
 * Here we should do the following HW initialization:
 *	(1) Program the mailbox register to initialize the GPCOM ring.
 *	(2) Set the ring read/write pointers to 0.
 *	(3) Send command "G2P_COMM_CMD_ID_INIT_GPCOM_RING" to PSP.
 *	(4) Wait/Poll for command response from PSP.
 */
static int g2p_hw_init(void *handle)
{
	u32 val;
	u32 ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dev_dbg(adev->dev, "%s called\n", __func__);

	/* For debug */
	g2p_print_status(adev);

	/* Write Ring Hi Physical address in MP0_MSP_MESSAGE_5 */
	val = upper_32_bits((u64)adev->psp.phys_addr);
	dev_dbg(adev->dev, "Ring physical address = %p\n", adev->psp.phys_addr);
	dev_dbg(adev->dev, "Ring high physical address = %x\n", val);
	WREG32(mmMP0_MSP_MESSAGE_5, val);

	/* Write Ring Lo Physical address in MP0_MSP_MESSAGE_6 */
	val = lower_32_bits((u64)adev->psp.phys_addr);
	dev_dbg(adev->dev, "Ring Physical address = %p\n", adev->psp.phys_addr);
	dev_dbg(adev->dev, "Ring Lo physical address = %x\n", val);
	WREG32(mmMP0_MSP_MESSAGE_6, val);

	/* Write Ring Size in MP0_MSP_MESSAGE_7 */
	WREG32(mmMP0_MSP_MESSAGE_7, RING_BUF_MAX_SIZE);

	dev_dbg(adev->dev, "Ring size written in MP0_7 = %d\n",
						RREG32(mmMP0_MSP_MESSAGE_7));

	/* Initialize Read and Write Ring pointers to 0 */
	WREG32(mmMP0_MSP_MESSAGE_3, 0);
	WREG32(mmMP0_MSP_MESSAGE_4, 0);

	/* Send command to PSP to initialize GPCOM Ring */
	val = G2P_COMM_CMD_ID_INIT_GPCOM_RING;
	WREG32(mmMP0_MSP_MESSAGE_0, val);

	/* Poll for the Response flag */
	do {
		val = RREG32(mmMP0_MSP_MESSAGE_0);
		if ((val & G2P_COMM_FLAG_RESPONSE) == G2P_COMM_FLAG_RESPONSE) {
			dev_info(adev->dev, "Received Response from PSP\n");
			break;
		}
		cpu_relax();
		dev_dbg(adev->dev, "Polling for Response from PSP\n");
	} while (true);

	ret = val & G2P_CMD_STATUS_MASK;
	if (ret) {
		dev_err(adev->dev, "command error: error code = %x\n", ret);
		return -EINVAL;
	}

	dev_info(adev->dev, "%s completed successfully\n", __func__);

	/* For debug */
	g2p_print_status(adev);

	hdcpss_init((void *)adev);

	return 0;
}

/*
 * This is the teardown routine for HW.
 * This function will be called from amdgpu_fini()
 * Here we should do the following HW teardown.
 *	(1) Send command G2P_COMM_CMD_ID_DESTROY_RINGS to PSP.
 *	(2) Poll for command response from PSP.
 *	(3) Set the Read and Write ring pointers to zero.
 *	(4) Return HW initialization status (success/failure)
 */
static int g2p_hw_fini(void *handle)
{
	u32 val;
	u32 ret;

	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dev_dbg(adev->dev, "%s called\n", __func__);

	/* Send Destroy rings command to PSP */
	val = G2P_COMM_CMD_ID_DESTROY_RINGS;
	WREG32(mmMP0_MSP_MESSAGE_0, val);

	/* Poll for the Response flag */
	do {
		val = RREG32(mmMP0_MSP_MESSAGE_0);
		if ((val & G2P_COMM_FLAG_RESPONSE) == G2P_COMM_FLAG_RESPONSE)
			break;
		cpu_relax();
	} while (true);

	ret = val & G2P_CMD_STATUS_MASK;
	if (ret) {
		dev_err(adev->dev, "command error: error code = %x\n", ret);
		return -EINVAL;
	}

	return 0;
}

static int g2p_suspend(void *handle)
{
	return 0;
}

static int g2p_resume(void *handle)
{
	return 0;
}

static void g2p_print_status(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dev_dbg(adev->dev, "%s called\n", __func__);

	dev_info(adev->dev, "Graphics Mailbox registers\n");
	dev_info(adev->dev, "mmMP0_MSP_MESSAGE_0=0x%08X\n",
						RREG32(mmMP0_MSP_MESSAGE_0));
	dev_info(adev->dev, "mmMP0_MSP_MESSAGE_1=0x%08X\n",
						RREG32(mmMP0_MSP_MESSAGE_1));
	dev_info(adev->dev, "mmMP0_MSP_MESSAGE_2=0x%08X\n",
						RREG32(mmMP0_MSP_MESSAGE_2));
	dev_info(adev->dev, "mmMP0_MSP_MESSAGE_3=0x%08X\n",
						RREG32(mmMP0_MSP_MESSAGE_3));
	dev_info(adev->dev, "mmMP0_MSP_MESSAGE_4=0x%08X\n",
						RREG32(mmMP0_MSP_MESSAGE_4));
	dev_info(adev->dev, "mmMP0_MSP_MESSAGE_5=0x%08X\n",
						RREG32(mmMP0_MSP_MESSAGE_5));
	dev_info(adev->dev, "mmMP0_MSP_MESSAGE_6=0x%08X\n",
						RREG32(mmMP0_MSP_MESSAGE_6));
	dev_info(adev->dev, "mmMP0_MSP_MESSAGE_7=0x%08X\n",
						RREG32(mmMP0_MSP_MESSAGE_7));
}

static int g2p_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int psp_set_clockgating_state(void *handle,
					enum amd_clockgating_state state)
{
	return 0;
}

static int psp_set_powergating_state(void *handle,
					enum amd_powergating_state state)
{
	return 0;
}

/*
 * This is the interrupt handler for MP0_SW_INT.
 * This function runs in Interrupt context.
 *	We want to do the following:
 *	(1) Acknowledge the interrupt
 *	(2) Wakeup the wait queues waiting for command completion interrupt.
 */
static int g2p_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	dev_info(adev->dev, "%s called\n", __func__);
	/* Interrupt will occur for frame completions */
	psp_data->is_resp_recvd = 1;

	/* Wake the thread waiting for frame completion */
	wake_up(&psp_data->psp_queue);

	/* Acknowledge the interrupt */
	WREG32(mmMP0_MSP_MESSAGE_7, PSP_MP0_SW_INT_ACK_VALUE);

	return 0;
}

int g2p_comm_register_client(u32 client_type, int(*callback)(void *))
{
	int ret = 0;
	struct client_context *client_data;

	if (psp_data->client_data[client_type] != NULL) {
		ret = -EINVAL;
		return ret;
	}

	client_data = vzalloc(sizeof(struct client_context));
	if (NULL == client_data) {
		ret = -ENOMEM;
		return ret;
	}
	mutex_lock(&psp_data->psp_mutex);
	client_data->type = client_type;
	client_data->callbackfunc = callback;
	psp_data->client_data[client_type] = client_data;
	mutex_unlock(&psp_data->psp_mutex);
	return ret;
}
EXPORT_SYMBOL(g2p_comm_register_client);

int g2p_comm_unregister_client(u32 client_type)
{
	int ret = 0;

	if (psp_data->client_data[client_type] == NULL) {
		ret = EINVAL;
		return ret;
	}
	mutex_lock(&psp_data->psp_mutex);
	psp_data->client_data[client_type] = NULL;
	vfree(psp_data->client_data[client_type]);
	mutex_unlock(&psp_data->psp_mutex);
	return ret;
}
EXPORT_SYMBOL(g2p_comm_unregister_client);

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

/*
 * This function will be used to send command buffers to GPU
 * by submitting the commands into the ring buffer frame.
 * Command buffer is allocated by the security service like HDCP SS.
 * Command buffer memory will be physically contiguous.
 */
int g2p_comm_send_command_buffer(void *cmd_buf, u32 cmd_size, u32 fence_val)
{
	int ret,val = 0;
	static u32 ring_index;
	struct amdgpu_device *adev = psp_data->adev;

	dev_dbg(adev->dev, "%s called\n", __func__);

	mutex_lock(&psp_data->psp_mutex);

	if (!cmd_buf) {
		dev_err(adev->dev, "Command buffer is NULL\n");
		mutex_unlock(&psp_data->psp_mutex);
		return -EINVAL;
	}

	if (cmd_size > 128) {
		dev_err(adev->dev, "Invalid command size\n");
		mutex_unlock(&psp_data->psp_mutex);
		return -EINVAL;
	}

	/* Initialize the GPCOM Frame for submission into GPCOM ring */
	psp_data->virt_addr[ring_index].cmd_buff_addr_hi =
					upper_32_bits(virt_to_phys(cmd_buf));
	psp_data->virt_addr[ring_index].cmd_buff_addr_lo =
					lower_32_bits(virt_to_phys(cmd_buf));
	psp_data->virt_addr[ring_index].cmd_buf_size =
					cmd_size;
	psp_data->virt_addr[ring_index].resv1 = 0;
	psp_data->virt_addr[ring_index].fence_addr_hi =
			upper_32_bits(virt_to_phys(psp_data->fence_addr));
	psp_data->virt_addr[ring_index].fence_addr_lo =
			lower_32_bits(virt_to_phys(psp_data->fence_addr));
	psp_data->virt_addr[ring_index].fence = fence_val;
	psp_data->virt_addr[ring_index].resv2 = 0;

	/* Dump the contents of the frame */
	dev_dbg(adev->dev, "Submitting frame at Ring Index = %d\n",
								ring_index);
	dev_dbg(adev->dev, "Command buffer Hi addr = %x\n",
			psp_data->virt_addr[ring_index].cmd_buff_addr_hi);
	dev_dbg(adev->dev, "Command buffer Lo addr = %x\n",
			psp_data->virt_addr[ring_index].cmd_buff_addr_lo);
	dev_dbg(adev->dev, "Command buffer size    = %d\n",
			psp_data->virt_addr[ring_index].cmd_buf_size);
	dev_dbg(adev->dev, "Resv 1 = %x\n",
			psp_data->virt_addr[ring_index].resv1);
	dev_dbg(adev->dev, "Fence Hi addr = %x\n",
			psp_data->virt_addr[ring_index].fence_addr_hi);
	dev_dbg(adev->dev, "Fence Lo addr = %x\n",
			psp_data->virt_addr[ring_index].fence_addr_lo);
	dev_dbg(adev->dev, "Fence value = %x\n",
			psp_data->virt_addr[ring_index].fence);
	dev_dbg(adev->dev, "Resv 2 = %x\n",
			psp_data->virt_addr[ring_index].resv2);

	/* Flush the command buffer */
	flush_buffer((void *)cmd_buf, cmd_size);

	/* Flush the buffer so that contents hit main memory */
	flush_buffer((void *)&psp_data->virt_addr[ring_index],
					sizeof(struct g2p_comm_rb_frame));

	if (ring_index >= (MAX_FRAMES - 1)) {
		ring_index = 0;
		val = 0;
	} else {
		val = RREG32(mmMP0_MSP_MESSAGE_3);
		val += RING_BUF_FRAME_SIZE / 4;
		ring_index++;
	}
	/* Increment the Write Pointer mailbox register for GPCOM ring */
	WREG32(mmMP0_MSP_MESSAGE_3, val);

	dev_dbg(adev->dev, "%s: val=%d\n", __func__, val);

	/* Wait for PSP interrupt for frame completion */
	ret = wait_event_timeout(psp_data->psp_queue,
					psp_data->is_resp_recvd == 1,
					msecs_to_jiffies
					(COMMAND_RESP_TIMEOUT));
	if (!ret) {
		dev_err(adev->dev, "Error - No response from PSP\n");
		mutex_unlock(&psp_data->psp_mutex);
		return -EINVAL;
	}

	/* Clear the flag */
	psp_data->is_resp_recvd = 0;

	mutex_unlock(&psp_data->psp_mutex);

	dev_dbg(adev->dev, "%s success\n", __func__);

	return 0;
}
EXPORT_SYMBOL(g2p_comm_send_command_buffer);

const struct amd_ip_funcs psp_ip_funcs = {
	.early_init = g2p_early_init,
	.late_init = NULL,
	.sw_init = g2p_sw_init,
	.sw_fini = g2p_sw_fini,
	.hw_init = g2p_hw_init,
	.hw_fini = g2p_hw_fini,
	.suspend = g2p_suspend,
	.resume	=  g2p_resume,
	.is_idle = NULL,
	.wait_for_idle = NULL,
	.soft_reset = NULL,
	.print_status = g2p_print_status,
	.set_clockgating_state = psp_set_clockgating_state,
	.set_powergating_state = psp_set_powergating_state,
};

static const struct amdgpu_irq_src_funcs g2p_irq_funcs = {
	.set = g2p_set_interrupt_state,
	.process = g2p_process_interrupt,
};

static void g2p_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->psp.irq.num_types = 1;
	adev->psp.irq.funcs = &g2p_irq_funcs;
}
