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

#ifndef _AMD_ACP_H
#define _AMD_ACP_H

#include <linux/types.h>

struct acp_irq_prv {
	struct device *dev;
	void __iomem *acp_mmio;
	int (*irq_handler)(struct device *dev);
	void (*enable_intr)(void __iomem *acp_mmio, int enable);
};

/* Public interface of ACP device */
struct amd_acp_device {
	void (*irq_register)(struct amd_acp_device *acp_dev, void *iprv);
	void (*irq_get)(struct amd_acp_device *acp_dev);
	void (*irq_put)(struct amd_acp_device *acp_dev);
};

#endif /* _AMD_ACP_H */
