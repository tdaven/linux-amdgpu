/*
* psp_comm_driver.c - communication driver between host and secure processor
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

#include "psp_comm_driver.h"
#include <linux/psp_comm_if.h>

static const struct pci_device_id driver_ids[] = {
	{ PCI_DEVICE(AMD_VENDOR_ID, CCP_CARRIZO_DEVICE_ID) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, driver_ids);

struct pci_driver psp_comm_pci_device = {
	.name = "psp comm pci device ",
	.id_table = driver_ids,
	.probe = psp_comm_pci_probe,
	.remove = psp_comm_pci_remove,
};
static void __iomem *_iobase;

struct psp_comm_pci_msix {
	u32 vector;
	char name[16];
};

struct psp_comm_pci {
	int msix_count;
	struct psp_comm_pci_msix msix[MSIX_VECTORS];
};

static struct psp_comm_pci *psp_comm_pci;

static struct device *dev;

static inline void *getpagestart(void *addr)
{
	return (void *)(((u64)(addr)) & PAGE_MASK);
}

static inline u32 getoffsetinpage(void *addr)
{
	return (u32)(((u64)(addr)) & (~PAGE_MASK));
}

static inline u32 getnrofpagesforbuffer(void *addrStart,
		u32 len)
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

static void invalidate_buffer(void *addr, u32 size)
{

}

int psp_comm_register_client(u32 client_type, int(*callback)(void *))
{
	int ret = 0;
	struct client_context *client_data;

	if (!psp_comm_data.psp_init_done) {
		dev_err(dev, "PSP initialization failed\n");
		return -ENODEV;
	}

	if (psp_comm_data.client_data[client_type] != NULL) {
		dev_err(dev, " %s : client already registered\n", __func__);
		ret = -EINVAL;
		return ret;
	}
	client_data = vzalloc(sizeof(struct client_context));
	if (NULL == client_data) {
		ret = -ENOMEM;
		dev_err(dev, " %s : alloc clientdata failed\n", __func__);
		return ret;
	}
	mutex_lock(&psp_comm_data.psp_comm_lock);
	client_data->type = client_type;
	client_data->callbackfunc = callback;
	psp_comm_data.client_data[client_type] = client_data;
	mutex_unlock(&psp_comm_data.psp_comm_lock);
	return ret;
}
EXPORT_SYMBOL(psp_comm_register_client);

int psp_comm_unregister_client(u32 client_type)
{
	int ret = 0;

	if (psp_comm_data.client_data[client_type] == NULL) {
		dev_dbg(dev, " %s : client not registered\n", __func__);
		ret = -EINVAL;
		return ret;
	}
	mutex_lock(&psp_comm_data.psp_comm_lock);
	vfree(psp_comm_data.client_data[client_type]);
	psp_comm_data.client_data[client_type] = NULL;
	mutex_unlock(&psp_comm_data.psp_comm_lock);
	return ret;
}
EXPORT_SYMBOL(psp_comm_unregister_client);

void psp_comm_process_work(struct work_struct *psp_comm_workqueue)
{
	u32 outqueuerdptr, outqueuewrtptr, client_type;
	struct psp_comm_buf *comm_buf;
	struct client_context **client_data;

	mutex_lock(&psp_comm_data.psp_comm_lock);

	client_data = psp_comm_data.client_data;
	comm_buf = psp_comm_data.psp_comm_virtual_addr;
	outqueuerdptr = psp_comm_read_out_queue_rdptr();
	outqueuewrtptr = psp_comm_read_out_queue_wrptr();

	while (outqueuerdptr != outqueuewrtptr) {
		dev_dbg(dev, "Outqueue:Readptr 0x%x;Writeptr 0x%x\n",
				outqueuerdptr, outqueuewrtptr);

		flush_buffer((void *)psp_comm_data
				.psp_comm_virtual_addr
				, sizeof(struct psp_comm_buf));

		if ((outqueuewrtptr > PSP_COMM_MAX_OUT_WRTPTR_VALUE) ||
				(outqueuerdptr > PSP_COMM_MAX_OUT_RDPTR_VALUE)
		   ) {
			dev_err(dev, "%s:Out queue: read ptr 0x%x",
					__func__, outqueuerdptr);
			dev_err(dev, "%s:Out queue: write ptr 0x%x",
					__func__, outqueuewrtptr);
			psp_comm_write_out_queue_rdptr(outqueuewrtptr);
			break;
		}

			if (!((comm_buf->outqueue[outqueuerdptr].session_id) <
						MAX_SESSIONS_SUPPORTED)) {
				dev_err(dev, "Invalid session ID 0x%x\n",
					comm_buf->outqueue[outqueuerdptr].
					session_id);
				mutex_unlock(&psp_comm_data.psp_comm_lock);
				psp_comm_write_out_queue_rdptr(outqueuewrtptr);
				break;
		}

		client_type = comm_buf->outqueue[outqueuerdptr].client_type;
		if (!(client_type < PSP_COMM_CLIENT_TYPE_INVALID)) {
			dev_dbg(dev, "Invalid client ID 0x%x\n", client_type);
			dev_dbg(dev, "SID 0x%x; payload=0x%x\n",
				comm_buf->outqueue[outqueuerdptr].session_id,
				comm_buf->outqueue[outqueuerdptr].payload);
			client_type = PSP_COMM_CLIENT_TYPE_TEE;
		}

		psp_comm_write_out_queue_rdptr((outqueuerdptr + 1) %
				PSP_COMM_MAX_QUEUES_ENTRIES);
		mutex_unlock(&psp_comm_data.psp_comm_lock);

		psp_comm_data.client_data[client_type]->callbackfunc
		((void *)&(comm_buf->outqueue[outqueuerdptr]));

		mutex_lock(&psp_comm_data.psp_comm_lock);
		outqueuerdptr = psp_comm_read_out_queue_rdptr();
		outqueuewrtptr = psp_comm_read_out_queue_wrptr();
	}
	mutex_unlock(&psp_comm_data.psp_comm_lock);
}

irqreturn_t psp_comm_interrupt_callback(int intr, void *pcontext)
{
	u32 outqueuerdptr, outqueuewrtptr;
	u32 cmd_resp;

	do {
		psp_comm_acknowledge_interrupt();
		cmd_resp = psp_comm_read_cmdresp_register();
		if ((cmd_resp & (PSP_COMM_MSB_MASK))) {

			psp_comm_data.command_status = TRUE;
			wake_up_interruptible(&psp_comm_data.command_wait);
		}
		outqueuerdptr = psp_comm_read_out_queue_rdptr();
		outqueuewrtptr = psp_comm_read_out_queue_wrptr();

		if (outqueuerdptr != outqueuewrtptr)
			queue_work(psp_comm_data.psp_comm_workqueue,
							&psp_comm_data.work);
	} while (FALSE);
	return IRQ_HANDLED;
}

static int psp_comm_get_msix_irqs(struct pci_dev *pdev)
{
	struct msix_entry msix_entry[MSIX_VECTORS];
	u32 name_len = sizeof(psp_comm_pci->msix[0].name) - 1;
	int v, ret = 0;

	do {
		for (v = 0; v < ARRAY_SIZE(msix_entry); v++)
			msix_entry[v].entry = v;

		while ((ret = pci_enable_msix(pdev, msix_entry, v)) > 0)
			v = ret;
		if (ret)
			break;

		psp_comm_pci->msix_count = v;
		for (v = 0; v < psp_comm_pci->msix_count; v++) {
			/* set the interrupt names and request the irqs */
			snprintf(psp_comm_pci->msix[v].name, name_len,
							"psp_comm-%u", v);
			psp_comm_pci->msix[v].vector = msix_entry[v].vector;
			ret = request_irq(psp_comm_pci->msix[v].vector,
					psp_comm_interrupt_callback,
					0,
					psp_comm_pci->msix[v].name, dev);
			if (ret) {
				dev_dbg(dev,
				"unable to allocate MSI-X IRQ (%d)\n", ret);
				while (v--)
					free_irq(psp_comm_pci->msix[v]
							.vector, dev);
				pci_disable_msix(pdev);
				psp_comm_pci->msix_count = 0;
				break;
			}
		}

	} while (FALSE);

	return ret;
}

static int psp_comm_get_msi_irq(struct pci_dev *pdev)
{
	int ret = 0;

	do {
		ret = pci_enable_msi(pdev);
		if (ret)
			break;
		ret = request_irq(pdev->irq,
				psp_comm_interrupt_callback,
				0, "ccp", dev);
		if (ret) {
			dev_dbg(dev,
				"unable to allocate MSI IRQ (%d)\n", ret);
			pci_disable_msi(pdev);
			break;
		}
	} while (FALSE);

	return ret;
}

int psp_comm_register_msi(struct pci_dev *pdev)
{
	int ret;

	do {
		ret = psp_comm_get_msix_irqs(pdev);
		if (!ret) {
			dev_dbg(dev,
				"enabled MSI-X interrupt %d\n", ret);
			break;
		}
		ret = psp_comm_get_msi_irq(pdev);
		if (!ret) {
			dev_dbg(dev, "enabled MSI interrupt %d\n", ret);
			break;
		}
	} while (FALSE);

	return ret;
}

void psp_comm_free_irqs(struct pci_dev *pdev)
{
	if (psp_comm_pci->msix_count) {
		while (psp_comm_pci->msix_count--)
			free_irq(psp_comm_pci->msix[psp_comm_pci->msix_count]
								.vector, dev);
		pci_disable_msix(pdev);
	} else {
		free_irq(pdev->irq, dev);
		pci_disable_msi(pdev);
	}
}

void psp_comm_pci_remove(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;

	psp_comm_free_irqs(pdev);
	if (psp_comm_pci)
		devm_kfree(dev, psp_comm_pci);
	pci_iounmap(pdev, _iobase);
	_iobase = NULL;
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	destroy_workqueue(psp_comm_data.psp_comm_workqueue);
	mutex_destroy(&psp_comm_data.psp_comm_lock);
	psp_comm_deallocate_memory((u64)psp_comm_data.psp_comm_virtual_addr,
			sizeof(struct psp_comm_buf));
	psp_comm_data.psp_init_done = 0;
}

int psp_comm_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;

	dev = &pdev->dev;
	do {
		dev_dbg(dev, " %s : psp comm probe called\n", __func__);
		ret = pci_enable_device(pdev);
		if (0 != ret)
			break;
		ret = pci_request_regions(pdev, "CRYPTO");
		if (0 != ret)
			break;
		pci_set_master(pdev);
		_iobase = pci_iomap(pdev, PSP_BAR_NUM, 0);
		if (_iobase == NULL) {
			pci_release_regions(pdev);
			pci_disable_device(pdev);
			ret = -EIO;
			break;
		}
		psp_comm_pci = devm_kzalloc(dev, sizeof(*psp_comm_pci),
								 GFP_KERNEL);
		if (!psp_comm_pci) {
			pci_iounmap(pdev, _iobase);
			_iobase = NULL;
			pci_release_regions(pdev);
			pci_disable_device(pdev);
			ret = -ENOMEM;
			break;
		}
		ret = psp_comm_register_msi(pdev);
		if (0 != ret) {
			devm_kfree(dev, psp_comm_pci);
			pci_iounmap(pdev, _iobase);
			_iobase = NULL;
			pci_release_regions(pdev);
			pci_disable_device(pdev);
			break;
		}
		ret = psp_comm_init();
		if (0 != ret) {
			devm_kfree(dev, psp_comm_pci);
			pci_iounmap(pdev, _iobase);
			_iobase = NULL;
			pci_release_regions(pdev);
			pci_disable_device(pdev);
			break;
		}
		psp_comm_data.psp_init_done = 1;
		dev_info(dev, " %s : psp comm probe successful\n", __func__);
	} while (FALSE);

	return ret;
}

void psp_comm_write_command(u32 cmd, u32 high, u32 low)
{
	dev_dbg(dev, "%s : high : %x low : %x\n", __func__, high, low);
	ioread32(_iobase + PSP_COMM_C2PMSG_17);
	iowrite32(high, _iobase + PSP_COMM_C2PMSG_18);
	iowrite32(low, _iobase + PSP_COMM_C2PMSG_19);
	iowrite32(cmd, _iobase + PSP_COMM_C2PMSG_17);
}

u32 psp_comm_read_cmdresp_register(void)
{
	return ioread32(_iobase + PSP_COMM_C2PMSG_17);
}

void psp_comm_trigger_interrupt(u32 data)
{
	iowrite32(data, _iobase + PSP_COMM_C2PMSG_20);
}

void psp_comm_acknowledge_interrupt(void)
{
	u32 status;

	status = ioread32(_iobase + PSP_COMM_P2CMSG_INTSTS);
	iowrite32(status, _iobase + PSP_COMM_P2CMSG_INTSTS);
}

void psp_comm_write_out_queue_rdptr(u32 data)
{
	iowrite32(data, _iobase + PSP_COMM_C2PMSG_23);
}

u32 psp_comm_read_out_queue_rdptr(void)
{
	return ioread32(_iobase + PSP_COMM_C2PMSG_23);
}

u32 psp_comm_read_out_queue_wrptr(void)
{
	return ioread32(_iobase + PSP_COMM_C2PMSG_22);
}

u32 psp_comm_read_in_queue_wrptr(void)
{
	return ioread32(_iobase + PSP_COMM_C2PMSG_20);
}

struct device *psp_comm_get_device(void)
{
	return dev;
}

int psp_comm_command(u32 command)
{
	int ret = 0;
	int command_resp, error_code = 0;
	u64 val = (u64) psp_comm_data.psp_comm_physical_addr;

	flush_buffer(psp_comm_data.psp_comm_virtual_addr,
			sizeof(struct psp_comm_buf));
	psp_comm_data.command_status = FALSE;
	psp_comm_write_command(command,	upper_32_bits(val), lower_32_bits(val));
	ret = wait_event_interruptible_timeout(psp_comm_data.command_wait,
			psp_comm_data.command_status
			== TRUE,
			msecs_to_jiffies(PSP_COMMAND_WAIT_TIMEOUT));

	if (ret < 1) {
		dev_dbg(dev, "%s: Timeout occurred\n", __func__);

		invalidate_buffer(psp_comm_data.psp_comm_virtual_addr,
				sizeof(struct psp_comm_buf));
		return -1;
	}

	command_resp = psp_comm_read_cmdresp_register();
	if ((command_resp & PSP_COMM_MSB_MASK)) {
		error_code = (command_resp & PSP_COMM_CMD_ERROR_MASK);
		if (error_code != 0) {
			dev_err(dev, "%s : error in command error Code %d\n",
					__func__, error_code);
		}
		ret = error_code;
	}

	invalidate_buffer(psp_comm_data.psp_comm_virtual_addr,
			sizeof(struct psp_comm_buf));
	return ret;
}

/* not required if MSI is working */
void psp_comm_wait_for_status(void)
{
	u32 status;

	dev_dbg(dev, " %s :\n", __func__);
	do {
		status = ioread32(_iobase + PSP_COMM_C2PMSG_17);
		if ((status & PSP_COMM_FLAG_RESPONSE) ==
					PSP_COMM_FLAG_RESPONSE)
			break;
		cpu_relax();
	} while (true);
}

u64 psp_comm_allocate_memory(u32 requestedsize)
{
	void *addr;

	dev_dbg(dev, " %s : req-sz 0x%llx\n",
			__func__, (u64)requestedsize);
	do {
		/* pages should be 4K aligned */
		addr = (void *)__get_free_pages(GFP_KERNEL,
						get_order(requestedsize));
		if (NULL == addr) {
			dev_err(dev,
				" %s : addr failed 0x%llx\n",
					__func__, (u64)addr);
			break;
		}
	} while (FALSE);
	dev_dbg(dev, " %s : addr addr 0x%llx\n",
			__func__, (u64)addr);
	return (u64)addr;
}

void psp_comm_deallocate_memory(u64 addr, u32 requestedsize)
{
	free_pages((u64)addr, get_order(requestedsize));
}

struct device *psp_get_device(void)
{
	return dev;
}

int psp_comm_send_buffer(u32 client_type, void *buf, u32 buf_size, u32 cmd_id)
{
	int ret = 0;
	struct psp_comm_buf *comm_buf;

	if (buf_size > PSP_COMM_CMD_BUF_MAX_SIZE) {
		dev_err(dev, " %s : max buffer size exceeded\n", __func__);
		ret = -EINVAL;
		return ret;
	}
	mutex_lock(&psp_comm_data.psp_comm_lock);

	comm_buf = (struct psp_comm_buf *)psp_comm_data.psp_comm_virtual_addr;
	comm_buf->bufsize = sizeof(struct psp_comm_buf);

	memcpy(&comm_buf->cmdbuf, buf, buf_size);
	cmd_id &= ~(PSP_COMM_CLIENT_TYPE_MASK << PSP_COMM_CLIENT_TYPE_SHIFT);
	cmd_id |= (client_type << PSP_COMM_CLIENT_TYPE_SHIFT);

	ret = psp_comm_command(cmd_id);
	memcpy(buf, &comm_buf->cmdbuf, buf_size);
	if (ret != 0) {
		dev_err(dev, " %s : error in sending command\n", __func__);
	}
	mutex_unlock(&psp_comm_data.psp_comm_lock);
	return ret;
}
EXPORT_SYMBOL(psp_comm_send_buffer);

int psp_comm_send_notification(u32 client_type, void *notification_data)
{
	int ret = 0;
	struct psp_comm_buf *comm_buf;
	u32 inqueuewrtptr = 0;
	u32 outqueuerdptr = 0;

	mutex_lock(&psp_comm_data.psp_comm_lock);
	if (*(u32 *)notification_data == 0) {
		dev_err(dev, " %s : invalid notification data\n", __func__);
		ret = -EINVAL;
		return ret;
	}

	comm_buf = (struct psp_comm_buf *)psp_comm_data.psp_comm_virtual_addr;
	inqueuewrtptr = psp_comm_read_in_queue_wrptr();
	outqueuerdptr = psp_comm_read_out_queue_rdptr();
	comm_buf->inqueue[inqueuewrtptr].session_id = *(u32 *)notification_data;
	comm_buf->outqueue[outqueuerdptr].client_type = client_type;
	inqueuewrtptr = (inqueuewrtptr + 1) % PSP_COMM_MAX_QUEUES_ENTRIES;
	psp_comm_trigger_interrupt(inqueuewrtptr);
	mutex_unlock(&psp_comm_data.psp_comm_lock);
	return ret;
}
EXPORT_SYMBOL(psp_comm_send_notification);

int psp_comm_init(void)
{
	int ret = -EIO;
	struct psp_comm_buf *comm_buf;

	dev = psp_get_device();

	do {
		mutex_init(&psp_comm_data.psp_comm_lock);
		init_waitqueue_head(&(psp_comm_data.command_wait));
		psp_comm_data.psp_comm_workqueue =
				create_workqueue("psp_comm_workqueue");
		if (!psp_comm_data.psp_comm_workqueue) {
			dev_err(dev, "%s :create_workqueue returned NULL\n",
								 __func__);
			ret = -ENOMEM;
			break;
		}
		INIT_WORK((struct work_struct *)&psp_comm_data.work,
						psp_comm_process_work);

		psp_comm_data.psp_comm_virtual_addr = (struct psp_comm_buf *)
			psp_comm_allocate_memory(sizeof
					(struct psp_comm_buf));
		if (NULL == psp_comm_data.psp_comm_virtual_addr) {
			dev_err(dev, " %s : alloc failed\n", __func__);
			destroy_workqueue(psp_comm_data.psp_comm_workqueue);
			ret = -ENOMEM;
			break;
		}

		psp_comm_data.psp_comm_physical_addr = (struct psp_comm_buf *)
			virt_to_phys((void *)
					psp_comm_data
					.psp_comm_virtual_addr);

		comm_buf = (struct psp_comm_buf *)psp_comm_data
						.psp_comm_virtual_addr;
		comm_buf->bufsize = sizeof(struct psp_comm_buf);
		comm_buf->bufversion = PSP_COMM_API_VERSION;
		comm_buf->inqueueelements = PSP_COMM_MAX_QUEUES_ENTRIES;
		comm_buf->outqueueelements = PSP_COMM_MAX_QUEUES_ENTRIES;

		ret = psp_comm_command(PSP_COMM_CMD_ID_INITIALIZE);

		if (ret != 0) {
			dev_err(dev, " %s : error in sending command\n",
					__func__);
			destroy_workqueue(psp_comm_data.psp_comm_workqueue);
			psp_comm_deallocate_memory((u64)psp_comm_data.
					psp_comm_virtual_addr,
					sizeof(struct psp_comm_buf));
			ret = -1;
			break;
		}

	} while (FALSE);

	dev_info(dev, " %s : psp comm init successful\n", __func__);
	return ret;
}

static int __init psp_comm_driver_init(void)
{
	int ret = -EIO;

	/* Set initial state of PSP initialization */
	psp_comm_data.psp_init_done = 0;

	do {
		ret = pci_register_driver(&psp_comm_pci_device);
		if (0 != ret) {
			dev_err(dev, " %s : PCI driver registeration failed\n",
					__func__);
			break;
		}
	} while (FALSE);

	dev_info(dev, " %s : psp comm driver initialized\n", __func__);

	return (int)ret;
}

static void __exit psp_comm_driver_exit(void)
{
	pci_unregister_driver(&psp_comm_pci_device);
}

module_init(psp_comm_driver_init);
module_exit(psp_comm_driver_exit);

MODULE_AUTHOR("Advanced Micro Devices, Inc.");
MODULE_DESCRIPTION("psp comm");
