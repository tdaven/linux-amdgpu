/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#ifndef __DAL_CONTROLLER_DCE110_H__
#define __DAL_CONTROLLER_DCE110_H__

#include "include/controller_interface.h"

#include "../controller.h"

struct controller_dce110 {
	struct controller base;
};

#define CONTROLLER_DCE110_FROM_BASE(controller_base) \
	container_of(controller_base, struct controller_dce110, base)

struct controller *dal_controller_dce110_create(
	struct controller_init_data *init_data);

bool dal_controller_dce110_construct(
	struct controller_dce110 *controller_dce110,
	struct controller_init_data *init_data);

void dal_controller_dce110_destruct(
	struct controller_dce110 *controller_dce110);

#endif

