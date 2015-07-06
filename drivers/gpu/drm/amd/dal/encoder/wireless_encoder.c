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

#include "dal_services.h"

/*
 * Pre-requisites: headers required by header of this unit
 */

#include "include/encoder_interface.h"
#include "include/gpio_interface.h"
#include "encoder_impl.h"

/*
 * Header of this unit
 */

#include "wireless_encoder.h"

/*
 * Post-requisites: headers required by this unit
 */

/*
 * This unit
 */

/*
 * @brief
 * Reports list of supported stream engines
 * Analog encoder supports exactly one engine - preferred one
 */
union supported_stream_engines
	dal_wireless_encoder_get_supported_stream_engines(
	const struct encoder_impl *enc)
{
	union supported_stream_engines result;

	result.u_all = (1 << enc->preferred_engine);

	return result;
}

bool dal_wireless_encoder_construct(
	struct wireless_encoder *enc,
	const struct encoder_init_data *init_data)
{
	struct encoder_impl *base = &enc->base;

	if (!dal_encoder_impl_construct(base, init_data)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	base->input_signals = SIGNAL_TYPE_WIRELESS;

	/* Wireless can use any DIG, so just default to the last one */

	switch (dal_graphics_object_id_get_encoder_id(base->id)) {
	case ENCODER_ID_INTERNAL_WIRELESS:
		base->preferred_engine = ENGINE_ID_VCE;
	break;
	default:
		base->preferred_engine = ENGINE_ID_UNKNOWN;
	}

	return true;
}

void dal_wireless_encoder_destruct(
	struct wireless_encoder *enc)
{
	dal_encoder_impl_destruct(&enc->base);
}
