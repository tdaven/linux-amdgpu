/*
* helper.h - kernel helper functions
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

#ifndef _HELPER_H_
#define _HELPER_H_

#include <linux/version.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/sizes.h>
#include <asm/pgtable.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/wait.h>

/* define types */
#if !defined(TRUE)
#define TRUE (1 == 1)
#endif

#if !defined(FALSE)
#define FALSE (1 != 1)
#endif

u32 sizetoorder(u32 size);
u32 addrtopfn(void *addr);
u32 get_pagealigned_size(u32 size);

void flush_buffer(void *addr, u32 size);
void invalidate_buffer(void *addr, u32 size);

#endif
