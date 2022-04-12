#  FreeRTOS STM32 Reference Integration
#
#  Copyright (C) 2022 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy of
#  this software and associated documentation files (the "Software"), to deal in
#  the Software without restriction, including without limitation the rights to
#  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
#  the Software, and to permit persons to whom the Software is furnished to do so,
#  subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all
#  copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
#  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
#  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
#  https://www.FreeRTOS.org
#  https://github.com/FreeRTOS
#

###############################################################################
# Copy auto generated files and scripts needed by the NSPE build.
###############################################################################
TFM_INTERFACE_FILES = ${shell sh -c 'find ${TFM_BUILD_PATH}/install/interface -type f 2>/dev/null'}
TFM_INTERFACE_HEADERS =  $(filter %.h,$(TFM_INTERFACE_FILES))
TFM_INTERFACE_HEADER_PATHS = ${subst ${TFM_BUILD_PATH}/install/interface/,${PROJECT_PATH}/tfm/interface/,${TFM_INTERFACE_HEADERS}}

TFM_S_VENEERS = ${TFM_BUILD_PATH}/install/interface/lib/s_veneers.o

TFM_LAYOUT_FILES = ${shell sh -c 'find ${TFM_BUILD_PATH}/install/image_signing/layout_files -type f 2>/dev/null'}
TFM_LAYOUT_PATHS = ${subst ${TFM_BUILD_PATH}/install/image_signing/layout_files/,${PROJECT_PATH}/tfm/layout_files/,${TFM_LAYOUT_FILES}}

TFM_SCRIPT_FILES = ${shell sh -c "find ${TFM_BUILD_PATH}/install/image_signing/scripts -type f -name '*.py' 2>/dev/null"}
TFM_SCRIPT_PATHS = ${subst ${TFM_BUILD_PATH}/install/image_signing/scripts/,${PROJECT_PATH}/tfm/scripts/,${TFM_SCRIPT_FILES}}

.DEFAULT_GOAL = all
.ONESHELL:

.PHONY: clean all

all: ${BUILD_PATH}/tfm/.generated ${PROJECT_PATH}/tfm/interface/libtfm_interface.a

${BUILD_PATH}/tfm/.generated : $(TFM_LAYOUT_PATHS) $(TFM_INTERFACE_HEADER_PATHS) $(TFM_SCRIPT_PATHS)
	mkdir -p "$(dir $@)"
	sh -c "touch ${BUILD_PATH}/tfm/.generated"

${PROJECT_PATH}/tfm/interface/% : ${TFM_BUILD_PATH}/install/interface/%
	mkdir -p "$(dir $@)"
	cp "$<" "$@"

TFM_INTERFACE_C = $(filter %.c,$(TFM_INTERFACE_FILES))
TFM_INTERFACE_OBJS = $(addprefix ${BUILD_PATH}/tfm/,$(subst .c,.o,$(notdir $(TFM_INTERFACE_C))))

${BUILD_PATH}/tfm/%.o : ${TFM_BUILD_PATH}/install/interface/src/%.c
	mkdir -p "$(dir $@)"
	arm-none-eabi-gcc -c -mcpu=cortex-m33 -std=gnu11 -g3 -O1 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -DTFM_PSA_API -DPS_ENCRYPTION -DTFM_PARTITION_PROTECTED_STORAGE -DTFM_PARTITION_INTERNAL_TRUSTED_STORAGE -DTFM_PARTITION_CRYPTO -DTFM_PARTITION_PLATFORM -DTFM_PARTITION_INITIAL_ATTESTATION -DTFM_LVL=2 -I ${TFM_BUILD_PATH}/install/interface/include ${cross_suffix} -o "$@" "$<"

${PROJECT_PATH}/tfm/interface/libtfm_interface.a : $(TFM_INTERFACE_OBJS) $(TFM_S_VENEERS)
	mkdir -p "$(dir $@)"
	arm-none-eabi-ar cr $@ $^

${PROJECT_PATH}/tfm/scripts/%.py : ${TFM_BUILD_PATH}/install/image_signing/scripts/%.py
	mkdir -p "$(dir $@)"
	cp "$<" "$@"

${PROJECT_PATH}/tfm/layout_files/%.o : ${TFM_BUILD_PATH}/install/image_signing/layout_files/%.o
	mkdir -p $(dir $@)
	cp "$<" "$@"

clean:
	rm -rf ${PROJECT_PATH}/tfm/interface/*.a
	rm -rf ${PROJECT_PATH}/tfm/interface/include/*
	rm -rf ${PROJECT_PATH}/tfm/scripts/*
	rm -rf ${PROJECT_PATH}/tfm/layout_files/*.o
