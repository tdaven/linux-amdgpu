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
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>

#include "acp_gfx_if.h"

#define ACP_MODE_I2S	0
#define ACP_MODE_AZ	1

#define VISLANDS30_IV_SRCID_ACP 0x000000a2
#define mmACP_AZALIA_I2S_SELECT 0x51d4

static int irq_set_source(void *private_data, unsigned src_id, unsigned type,
								int enabled)
{
	struct acp_irq_prv *idata = private_data;

	if (src_id == VISLANDS30_IV_SRCID_ACP) {
		idata->enable_intr(idata->acp_mmio, enabled);
		return 0;
	} else {
		return -1;
	}
}

static int irq_handler(void *private_data, unsigned src_id,
		       const uint32_t *iv_entry)
{
	struct acp_irq_prv *idata = private_data;

	if (src_id == VISLANDS30_IV_SRCID_ACP)
		return idata->irq_handler(idata->dev);
	else
		return -1;
}

static void acp_irq_register(struct amd_acp_device *acp_dev, void *iprv)
{
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	cgs_add_irq_source(acp_prv->cgs_device, VISLANDS30_IV_SRCID_ACP, 1,
			   irq_set_source, irq_handler, iprv);
}

static void acp_irq_get(struct amd_acp_device *acp_dev)
{
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	cgs_irq_get(acp_prv->cgs_device, VISLANDS30_IV_SRCID_ACP, 0);
}

static void acp_irq_put(struct amd_acp_device *acp_dev)
{
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	cgs_irq_put(acp_prv->cgs_device, VISLANDS30_IV_SRCID_ACP, 0);
}

void amd_acp_resume(struct amd_acp_private *acp_private)
{

}


void amd_acp_suspend(struct amd_acp_private *acp_private)
{

}

int amd_acp_hw_init(void *cgs_device,
		    unsigned acp_version_major, unsigned acp_version_minor,
		    struct amd_acp_private **acp_private)
{
	unsigned int acp_mode = ACP_MODE_I2S;

	if ((acp_version_major == 2) && (acp_version_minor == 2))
		acp_mode = cgs_read_register(cgs_device,
					mmACP_AZALIA_I2S_SELECT);

	if (acp_mode != ACP_MODE_I2S)
		return -ENODEV;

	*acp_private = kzalloc(sizeof(struct amd_acp_private), GFP_KERNEL);
	if (*acp_private == NULL)
		return -ENOMEM;

	(*acp_private)->cgs_device = cgs_device;
	(*acp_private)->acp_version_major = acp_version_major;
	(*acp_private)->acp_version_minor = acp_version_minor;

	(*acp_private)->public.irq_register = acp_irq_register;
	(*acp_private)->public.irq_get = acp_irq_get;
	(*acp_private)->public.irq_put = acp_irq_put;

	return 0;
}

int amd_acp_hw_fini(struct amd_acp_private *acp_private)
{
	kfree(acp_private);
	return 0;
}
