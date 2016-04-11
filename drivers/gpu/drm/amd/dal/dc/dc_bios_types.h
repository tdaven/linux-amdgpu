/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef DC_BIOS_TYPES_H
#define DC_BIOS_TYPES_H

/******************************************************************************
 * Interface file for VBIOS implementations.
 *
 * The default implementation is inside DC.
 * Display Manager (which instantiates DC) has the option to supply it's own
 * (external to DC) implementation of VBIOS, which will be called by DC, using
 * this interface.
 * (The intended use is Diagnostics, but other uses may appear.)
 *****************************************************************************/

#include "include/bios_parser_types.h"

uint8_t dc_bios_get_connectors_number(struct dc_bios *bios);

void dc_bios_power_up(struct dc_bios *bios);

uint8_t dc_bios_get_encoders_number(struct dc_bios *bios);
uint32_t dc_bios_get_oem_ddc_lines_number(struct dc_bios *bios);

struct graphics_object_id dc_bios_get_encoder_id(struct dc_bios *bios,
						 uint32_t i);
struct graphics_object_id dc_bios_get_connector_id(struct dc_bios *bios,
						   uint8_t connector_index);
uint32_t dc_bios_get_src_number(struct dc_bios *bios,
				struct graphics_object_id id);
uint32_t dc_bios_get_dst_number(struct dc_bios *bios,
				struct graphics_object_id id);

uint32_t dc_bios_get_gpio_record(struct dc_bios *dcb,
				 struct graphics_object_id id,
				 struct bp_gpio_cntl_info *gpio_record,
				 uint32_t record_size);
enum bp_result dc_bios_get_src_obj(struct dc_bios *bios,
				    struct graphics_object_id object_id,
				    uint32_t index,
				    struct graphics_object_id *src_object_id);
enum bp_result dc_bios_get_dst_obj(struct dc_bios *bios,
				   struct graphics_object_id object_id,
				   uint32_t index,
				   struct graphics_object_id *dest_object_id);
enum bp_result dc_bios_get_oem_ddc_info(struct dc_bios *bios,
					uint32_t index,
					struct graphics_object_i2c_info *info);
enum bp_result dc_bios_get_i2c_info(struct dc_bios *dcb,
				    struct graphics_object_id id,
				    struct graphics_object_i2c_info *info);

enum bp_result dc_bios_get_voltage_ddc_info(struct dc_bios *bios,
					    uint32_t index,
					    struct graphics_object_i2c_info *info);

enum bp_result dc_bios_get_thermal_ddc_info(struct dc_bios *bios,
					    uint32_t i2c_channel_id,
					    struct graphics_object_i2c_info *info);
enum bp_result dc_bios_get_hpd_info(struct dc_bios *bios,
				    struct graphics_object_id id,
				    struct graphics_object_hpd_info *info);
enum bp_result dc_bios_get_device_tag(struct dc_bios *bios,
				      struct graphics_object_id connector_object_id,
				      uint32_t device_tag_index,
				      struct connector_device_tag_info *info);

enum bp_result dc_bios_get_firmware_info(struct dc_bios *bios,
					 struct firmware_info *info);

enum bp_result dc_bios_get_spread_spectrum_info(struct dc_bios *bios,
						enum as_signal_type signal,
						uint32_t index,
						struct spread_spectrum_info *ss_info);
uint32_t dc_bios_get_ss_entry_number(struct dc_bios *bios,
				     enum as_signal_type signal);
enum bp_result dc_bios_get_embedded_panel_info(struct dc_bios *bios,
					       struct embedded_panel_info *info);
enum bp_result dc_bios_enum_embedded_panel_patch_mode(struct dc_bios *bios,
						      uint32_t index,
						      struct embedded_panel_patch_mode *mode);

enum bp_result dc_bios_get_gpio_pin_info(struct dc_bios *bios,
					 uint32_t gpio_id,
					 struct gpio_pin_info *info);

enum bp_result dc_bios_get_faked_edid_len(struct dc_bios *bios,
					  uint32_t *len);

enum bp_result dc_bios_get_faked_edid_buf(struct dc_bios *bios,
					  uint8_t *buff,
					  uint32_t len);

enum bp_result dc_bios_get_encoder_cap_info(struct dc_bios *bios,
					    struct graphics_object_id object_id,
					    struct bp_encoder_cap_info *info);
enum bp_result dc_bios_get_din_connector_info(struct dc_bios *bios,
					      struct graphics_object_id id,
					      struct din_connector_info *info);
bool dc_bios_is_device_id_supported(struct dc_bios *bios,
				    struct device_id id);
bool dc_bios_is_accelerated_mode(struct dc_bios *bios);
bool dc_bios_is_lid_open(struct dc_bios *bios);
#if 0 /* unused trap to debugger functions */

bool dc_bios_is_lid_status_changed(struct dc_bios *bios);

enum lcd_scale  dc_bios_get_scratch_lcd_scale(struct dc_bios *bios);
void dc_bios_get_bios_event_info(struct dc_bios *bios,
				 struct bios_event_info *info);
void dc_bios_update_requested_backlight_level(struct dc_bios *bios,
					      uint32_t backlight_8bit);
uint32_t dc_bios_get_requested_backlight_level(struct dc_bios *bios);
void dc_bios_take_backlight_control(struct dc_bios *bios,
				    bool cntl);
bool dc_bios_is_active_display(struct dc_bios *bios,
			       enum signal_type signal,
			       const struct connector_device_tag_info *device_tag);
enum controller_id dc_bios_get_embedded_display_controller_id(struct dc_bios *bios);
uint32_t dc_bios_get_embedded_display_refresh_rate(struct dc_bios *bios);
#endif

void dc_bios_prepare_scratch_active_and_requested(struct dc_bios *bios,
						  enum controller_id controller_id,
						  enum signal_type signal,
						  const struct connector_device_tag_info *device_tag);
void dc_bios_set_scratch_acc_mode_change(struct dc_bios *bios);

/* COMMANDS */
enum bp_result dc_bios_encoder_control(struct dc_bios *bios,
				       struct bp_encoder_control *cntl);
enum bp_result dc_bios_transmitter_control(struct dc_bios *bios,
					   struct bp_transmitter_control *cntl);
enum bp_result dc_bios_crt_control(struct dc_bios *bios,
				   enum engine_id engine_id,
				   bool enable,
				   uint32_t pixel_clock);
enum bp_result dc_bios_enable_crtc(struct dc_bios *bios,
				   enum controller_id id,
				   bool enable);
enum bp_result dc_bios_adjust_pixel_clock(struct dc_bios *bios,
					  struct bp_adjust_pixel_clock_parameters *bp_params);
enum bp_result dc_bios_set_pixel_clock(struct dc_bios *bios,
				       struct bp_pixel_clock_parameters *bp_params);
enum bp_result dc_bios_set_dce_clock(struct dc_bios *bios,
				     struct bp_set_dce_clock_parameters *bp_params);
enum bp_result dc_bios_enable_spread_spectrum_on_ppll(struct dc_bios *bios,
						      struct bp_spread_spectrum_parameters *bp_params,
						      bool enable);
enum bp_result dc_bios_program_crtc_timing(struct dc_bios *bios,
					   struct bp_hw_crtc_timing_parameters *bp_params);
enum bp_result dc_bios_blank_crtc(struct dc_bios *bios,
				  struct bp_blank_crtc_parameters *bp_params,
				  bool blank);
enum bp_result dc_bios_set_overscan(struct dc_bios *bios,
				    struct bp_hw_crtc_overscan_parameters *bp_params);
enum bp_result dc_bios_crtc_source_select(struct dc_bios *bios,
					  struct bp_crtc_source_select *bp_params);
enum bp_result dc_bios_program_display_engine_pll(struct dc_bios *bios,
						  struct bp_pixel_clock_parameters *bp_params);
enum bp_result dc_bios_get_divider_for_target_display_clock(struct dc_bios *bios,
							    struct bp_display_clock_parameters *bp_params);
enum signal_type dc_bios_dac_load_detect(struct dc_bios *bios,
					 struct graphics_object_id encoder,
					 struct graphics_object_id connector,
					 enum signal_type display_signal);
enum bp_result dc_bios_enable_memory_requests(struct dc_bios *bios,
					      enum controller_id controller_id,
					      bool enable);
enum bp_result dc_bios_external_encoder_control(struct dc_bios *bios,
						struct bp_external_encoder_control *cntl);
enum bp_result dc_bios_enable_disp_power_gating(struct dc_bios *bios,
						enum controller_id controller_id,
						enum bp_pipe_control_action action);


void dc_bios_post_init(struct dc_bios *bios,
		       struct adapter_service *as);

struct integrated_info *dc_bios_create_integrated_info(struct dc_bios *bios);

void dc_bios_destroy_integrated_info(struct dc_bios *dcb,
				     struct integrated_info **info);

struct dc_bios;

#endif /* DC_BIOS_TYPES_H */
