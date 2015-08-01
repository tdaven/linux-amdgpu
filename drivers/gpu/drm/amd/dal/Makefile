#
# Makefile for the DAL (Display Abstract Layer), which is a  sub-component
# of the AMDGPU drm driver.
# It provides the HW control for display related functionalities.

AMDDALPATH = $(RELATIVE_AMD_DAL_PATH)

subdir-ccflags-y += -I$(AMDDALPATH)/ -I$(AMDDALPATH)/include -DDAL_CZ_BRINGUP

DAL_LIBS = adapter amdgpu_dm audio asic_capability basics bios connector \
	controller dcs display_path encoder gpio gpu hw_sequencer i2caux irq \
	link_service mode_manager timing_service topology

AMD_DAL = $(addsuffix /Makefile, $(addprefix $(FULL_AMD_DAL_PATH)/,$(DAL_LIBS)))

include $(AMD_DAL)
