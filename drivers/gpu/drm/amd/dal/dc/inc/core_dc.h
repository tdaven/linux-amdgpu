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

#ifndef __CORE_DC_H__
#define __CORE_DC_H__

#include "core_types.h"
#include "hw_sequencer.h"

struct dc {
	struct dc_context *ctx;

	uint8_t link_count;
	struct core_link *links[MAX_PIPES * 2];

	/* TODO: determine max number of targets*/
	struct validate_context current_context;
	struct resource_pool res_pool;

	/*Power State*/
	enum dc_video_power_state previous_power_state;
	enum dc_video_power_state current_power_state;

	/* Inputs into BW and WM calculations. */
	struct bw_calcs_dceip bw_dceip;
	struct bw_calcs_vbios bw_vbios;

	/* HW functions */
	struct hw_sequencer_funcs hwss;
};

#endif /* __CORE_DC_H__ */
