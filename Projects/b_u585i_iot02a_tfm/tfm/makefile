#  FreeRTOS STM32 Reference Integration
#
#  Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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

# WORKSPACE_PATH, BUILD_PATH, and PROJECT_NAME must be provided by the caller.
# This can be done either as an agrument to make or in the enviornment.
# Other vars may be overridden as needed.

###############################################################################
# Config
###############################################################################
.NOTPARALLEL:
.ONESHELL:
.DEFAULT_GOAL = all
# MAKEFLAGS += --no-builtin-rules

# Normalize Paths
BUILD_PATH := $(realpath $(BUILD_PATH))
WORKSPACE_PATH := $(realpath $(WORKSPACE_PATH))

###############################################################################
# Path Definitions
###############################################################################
TOOLS_PATH = ${WORKSPACE_PATH}/tools
MIDDLEWARE_PATH = $(realpath ${WORKSPACE_PATH}/Middleware)
TFM_SRC_PATH = $(realpath ${MIDDLEWARE_PATH}/ARM/trusted-firmware-m)
MBEDTLS_SRC_PATH = $(realpath ${MIDDLEWARE_PATH}/ARM/mbedtls)
MCUBOOT_SRC_PATH = $(realpath ${MIDDLEWARE_PATH}/ARM/mcuboot)
PROJECT_PATH = $(realpath ${BUILD_PATH}/..)
TFM_BUILD_PATH = $(BUILD_PATH)/tfm_build
S_REGION_SIGNING_KEY = ${TFM_SRC_PATH}/bl2/ext/mcuboot/root-RSA-3072.pem
NS_REGION_SIGNING_KEY = ${TFM_SRC_PATH}/bl2/ext/mcuboot/root-RSA-3072_1.pem

###############################################################################
# Version number definition
###############################################################################
SPE_VERSION = "1.5.0"
NSPE_VERSION = "1.0.0"
NUM_PROCESSORS = 8

###############################################################################
# Default / phony targets
###############################################################################
.PHONY: info clean distclean \
		spe_patch_libs_clean spe_patch_libs_distclean \
		spe_build_reqs spe_build spe_bin \
		spe_clean spe_distclean \
		tfm_generated_files \
		nspe_build_reqs nspe_build_reqs_clean \
		spe_signed nspe_signed update factory_signed \
		tfm_scripts \
		all

###############################################################################
# Write Paths to stdout
###############################################################################
info:
	@echo "PROJECT_NAME:     ${PROJECT_NAME}"
	@echo "WORKSPACE_PATH:   ${WORKSPACE_PATH}"
	@echo "BUILD_PATH:       ${BUILD_PATH}"
	@echo "TOOLS_PATH:       ${TOOLS_PATH}"
	@echo "MIDDLEWARE_PATH:  ${MIDDLEWARE_PATH}"
	@echo "PROJECT_PATH:     ${PROJECT_PATH}"
	@echo "TFM_BUILD_PATH:   ${TFM_BUILD_PATH}"
	@echo "TFM_SRC_PATH:     ${TFM_SRC_PATH}"
	@echo "MBEDTLS_SRC_PATH: ${MBEDTLS_SRC_PATH}"
	@echo "MCUBOOT_SRC_PATH: ${MCUBOOT_SRC_PATH}"
	@echo "SHELLFLAGS:       ${.SHELLFLAGS}"
	@echo "CFLAGS:			 ${CFLAGS}"
	@echo "LDFLAGS:			 ${LDFLAGS}"

###############################################################################
# Rules to apply TF-M patches to mcuboot and mbedtls
###############################################################################
MBEDTLS_PATCHES := $(wildcard ${TFM_SRC_PATH}/lib/ext/mbedcrypt/*.patch)
MBEDTLS_PATCH_FLAGS = $(foreach file,$(notdir ${MBEDTLS_PATCHES}),${MBEDTLS_SRC_PATH}/$(basename ${file}).patched)
MBEDTLS_PATCHES_APPLIED = ${wildcard ${MBEDTLS_SRC_PATH}/*.patched}

# Apply each mbedtls patch
${MBEDTLS_SRC_PATH}/%.patched : ${TFM_SRC_PATH}/lib/ext/mbedcrypto/%.patch
	@echo Applying mbedtls patch: $(notdir $<)
	git -C ${MBEDTLS_SRC_PATH} apply $<
	cp $< $@
	touch $@

MCUBOOT_PATCHES := $(wildcard ${TFM_SRC_PATH}/lib/ext/mcuboot/*.patch)
MCUBOOT_PATCH_FLAGS = $(foreach file,$(notdir ${MCUBOOT_PATCHES}),${MCUBOOT_SRC_PATH}/$(basename ${file}).patched)
MCUBOOT_PATCHES_APPLIED = ${wildcard ${MCUBOOT_SRC_PATH}/*.patched}

# Apply each mcuboot patch
${MCUBOOT_SRC_PATH}/%.patched : ${TFM_SRC_PATH}/lib/ext/mcuboot/%.patch
	@echo Applying mcuboot patch: $(notdir $<)
	git -C ${MCUBOOT_SRC_PATH} apply $<
	cp $< $@
	touch $@

# Remove any applied mcuboot and mbetls patches
spe_patch_libs_clean :
ifneq ($(MBEDTLS_PATCHES_APPLIED),)
	@echo Removing applied mbedtls patches
	git -C ${MBEDTLS_SRC_PATH} apply --reverse ${MBEDTLS_PATCHES_APPLIED}
	rm -f ${MBEDTLS_PATCHES_APPLIED}
endif

ifneq ($(MCUBOOT_PATCHES_APPLIED),)
	@echo Removing applied mcuboot patches
	git -C ${MCUBOOT_SRC_PATH} apply --reverse ${MCUBOOT_PATCHES_APPLIED}
	rm -f ${MCUBOOT_PATCHES_APPLIED}
endif

# Forcibly remove all applied patches (will clobber any other local changes)
spe_patch_libs_distclean :
	git -C ${MBEDTLS_SRC_PATH} clean -f
	git -C ${MCUBOOT_PATCHES} clean -f

###############################################################################
# Generate the build system for TF-M "SPE" image
###############################################################################

# Use cmake to generate the Makefile file
${TFM_BUILD_PATH}/Makefile : ${MBEDTLS_PATCH_FLAGS} ${MCUBOOT_PATCH_FLAGS}
	mkdir -p ${TFM_BUILD_PATH}

	source ${TOOLS_PATH}/env_setup.sh
	cmake -S ${TFM_SRC_PATH} -B ${TFM_BUILD_PATH} \
		-DTFM_SPM_LOG_LEVEL_DEBUG=1 \
		-DTFM_PLATFORM=stm/b_u585i_iot02a \
		-DTFM_TOOLCHAIN_FILE=${TFM_SRC_PATH}/toolchain_GNUARM.cmake \
		-DCMAKE_BUILD_TYPE=Relwithdebinfo \
		-DTFM_DEV_MODE=1 \
		-DMBEDCRYPTO_PATH=${MBEDTLS_SRC_PATH} \
		-DMCUBOOT_PATH=${MCUBOOT_SRC_PATH} \
		-DTFM_PROFILE=profile_large \
		-DTFM_ISOLATION_LEVEL=2 \
		-DPython_FIND_VIRTUALENV=FIRST \
		-DCMAKE_MAKE_PROGRAM=${MAKE} \
		-DCMAKE_OBJECT_PATH_MAX=1024 \
		-DTFM_PARTITION_FIRMWARE_UPDATE=ON \
		-DMCUBOOT_DATA_SHARING=ON \
		-G"Unix Makefiles" \
		-DCONFIG_TFM_FP=hard \
		-DTFM_EXCEPTION_INFO_DUMP=on \
		-DNS=1
	sleep 1


###############################################################################
# Artifacts generated by a TF-M build
###############################################################################
TFM_BIN_ARTIFACTS := tfm_s.axf \
                     tfm_s.elf \
                     tfm_s.bin \
                     tfm_s.hex \
                     tfm_s.map

BL2_BIN_ARTIFACTS := bl2.axf \
                     bl2.elf \
                     bl2.bin \
                     bl2.hex \
                     bl2.map

###############################################################################
# Other artifacts generated by the tfm build system
###############################################################################
TFM_ARTIFACTS := ${addprefix bin/,${TFM_BIN_ARTIFACTS}} \
				 ${addprefix bin/,${BL2_BIN_ARTIFACTS}} \
				 image_macros_to_preprocess_bl2.c \
				 tfm_config_export.h \
				 region_defs.h

TFM_ARTIFACT_PATHS := ${addprefix ${TFM_BUILD_PATH}/,${TFM_ARTIFACTS}}

tfm_bin : ${TFM_ARTIFACT_PATHS}

###############################################################################
# Build TF-M Artifacts / SPE image
###############################################################################
${TFM_ARTIFACT_PATHS} &: ${TFM_BUILD_PATH}/Makefile
	$(MAKE) -C ${TFM_BUILD_PATH} all install
	$(MAKE) -f generated.mk PROJECT_PATH=${PROJECT_PATH} TFM_BUILD_PATH=${TFM_BUILD_PATH} all

###############################################################################
# Copy TF-M artifacts to the main build directory and rename
###############################################################################
TFM_BIN = ${subst tfm_,${BUILD_PATH}/${PROJECT_NAME}_,${TFM_BIN_ARTIFACTS}}

${BUILD_PATH}/${PROJECT_NAME}_% : ${TFM_BUILD_PATH}/bin/tfm_%
	cp "$<" "$@"

BL2_BIN = ${addprefix ${BUILD_PATH}/${PROJECT_NAME}_,${BL2_BIN_ARTIFACTS}}

${BUILD_PATH}/${PROJECT_NAME}_bl2% : ${TFM_BUILD_PATH}/bin/bl2%
	cp "$<" "$@"

spe_bin : ${BL2_BIN} ${TFM_BIN}

###############################################################################
# Clean TF-M / SPE build directory
###############################################################################
spe_clean :
	if [ -d ${TFM_BUILD_PATH} ]; then make -C ${TFM_BUILD_PATH} clean; fi
	rm -f ${TFM_BIN} ${BL2_BIN}

spe_distclean : spe_clean clean
	@echo Removing tfm build directory
	rm -rf ${TFM_BUILD_PATH}

###############################################################################
# List files to be copied into project build directory with SPE and BL2 bins
###############################################################################
TFM_INTF_LIB = ${PROJECT_PATH}/tfm/interface/libtfm_interface.a

###############################################################################
# Build tfm interface library.
###############################################################################
$(TFM_INTF_LIB) : ${TFM_BUILD_PATH}/app/libtfm_api_ns.a
	cp $^ $@
#	$(MAKE) -f generated.mk PROJECT_PATH=${PROJECT_PATH} TFM_BUILD_PATH=${TFM_BUILD_PATH} all

${PROJECT_PATH}/tfm/interface/libtfm_s_veneers.a : ${TFM_BUILD_PATH}/secure_fw/libtfm_s_veneers.a
	cp $^ $@

###############################################################################
# Generate the linker script for the NSPE partition based on the template
###############################################################################
${BUILD_PATH}/stm32u5xx_ns.ld : ${TFM_SRC_PATH}/platform/ext/target/stm/common/hal/template/gcc/appli_ns.ld ${TFM_BUILD_PATH}/tfm_config_export.h
	arm-none-eabi-gcc -E -P -xc \
		-DBL2 \
		-DBL2_HEADER_SIZE=0x400 \
		-DBL2_TRAILER_SIZE=0x2000 \
		-DDAUTH_CHIP_DEFAULT \
		-DMCUBOOT_IMAGE_NUMBER=2 \
		-DMCUBOOT_SIGN_RSA \
		-DMCUBOOT_SIGN_RSA_LEN=3072 \
		-DSTM32U585xx \
		-DTFM_PARTITION_CRYPTO \
		-DTFM_PARTITION_INTERNAL_TRUSTED_STORAGE \
		-DTFM_PARTITION_LOG_LEVEL=TFM_PARTITION_LOG_LEVEL_INFO \
		-DTFM_PARTITION_PLATFORM \
		-DTFM_PARTITION_PROTECTED_STORAGE \
		-DTFM_PSA_API \
		-DTFM_SP_LOG_RAW_ENABLED \
		-DUSE_HAL_DRIVER  \
		-DCONFIG_TFM_FP=hard \
		-I ${TFM_BUILD_PATH} -include ${TFM_BUILD_PATH}/tfm_config_export.h \
		-o $@ $<

###############################################################################
# Preprocess image_macros_to_preprocess_bl2.c
###############################################################################
${BUILD_PATH}/image_macros_preprocessed_bl2.c : ${TFM_BUILD_PATH}/image_macros_to_preprocess_bl2.c
	arm-none-eabi-gcc -E -P -xc -DTFM_PSA_API \
		-I ${TFM_BUILD_PATH} \
		-DBL2 \
		-DBL2_HEADER_SIZE=0x400 \
		-DBL2_TRAILER_SIZE=0x2000 \
		-DDAUTH_CHIP_DEFAULT \
		-DMCUBOOT_IMAGE_NUMBER=2 \
		-DMCUBOOT_SIGN_RSA \
		-DMCUBOOT_SIGN_RSA_LEN=3072 \
		-DSTM32U585xx \
		-DTFM_PARTITION_CRYPTO \
		-DTFM_PARTITION_INTERNAL_TRUSTED_STORAGE \
		-DTFM_PARTITION_LOG_LEVEL=TFM_PARTITION_LOG_LEVEL_INFO \
		-DTFM_PARTITION_PLATFORM \
		-DTFM_PARTITION_PROTECTED_STORAGE \
		-DTFM_PSA_API \
		-DTFM_SP_LOG_RAW_ENABLED \
		-DUSE_HAL_DRIVER \
		-DCONFIG_TFM_FP=hard \
		-include ${TFM_BUILD_PATH}/tfm_config_export.h \
		-o $@ \
		${TFM_BUILD_PATH}/image_macros_to_preprocess_bl2.c

###############################################################################
# Export the macros from image_macros_to_preprocess_bl2.c into a shell script
###############################################################################
${BUILD_PATH}/image_defs.sh : ${BUILD_PATH}/image_macros_preprocessed_bl2.c
	source ${TOOLS_PATH}/env_setup.sh
	python ${TOOLS_PATH}/macro_to_kvfile.py ${BUILD_PATH}/image_defs.sh ${BUILD_PATH}/image_macros_preprocessed_bl2.c

nspe_build_reqs_clean:
	rm -f ${BUILD_PATH}/image_macros_preprocessed_bl2.c
	rm -f ${BUILD_PATH}/stm32u5xx_ns.ld
	rm -f ${BUILD_PATH}/image_defs.sh
	$(MAKE) -f generated.mk clean

###############################################################################
# Define phony targets for signed images
###############################################################################
spe_signed : ${BUILD_PATH}/${PROJECT_NAME}_s_signed.bin

nspe_signed : ${BUILD_PATH}/${PROJECT_NAME}_ns_signed.bin

###############################################################################
# Define phony targets for factory and update images
###############################################################################
update : ${BUILD_PATH}/${PROJECT_NAME}_s_update.hex \
	     ${BUILD_PATH}/${PROJECT_NAME}_ns_update.hex \
		 ${BUILD_PATH}/${PROJECT_NAME}_s_ns_update.hex

factory : ${BUILD_PATH}/${PROJECT_NAME}_s_signed.hex \
	      ${BUILD_PATH}/${PROJECT_NAME}_ns_signed.hex \
		  ${BUILD_PATH}/${PROJECT_NAME}_s_ns_signed.hex \
		  ${BUILD_PATH}/${PROJECT_NAME}_s_ns_signed.bin \
		  ${BUILD_PATH}/${PROJECT_NAME}_bl2.bin \
	      ${BUILD_PATH}/${PROJECT_NAME}_bl2.hex \
		  ${BUILD_PATH}/${PROJECT_NAME}_bl2_s_ns_factory.hex

###############################################################################
# Rule to generate combined s and ns "factory" images
###############################################################################
${BUILD_PATH}/${PROJECT_NAME}_s_ns% : ${BUILD_PATH}/${PROJECT_NAME}_s% ${BUILD_PATH}/${PROJECT_NAME}_ns%
	source ${TOOLS_PATH}/env_setup.sh
	python ${PROJECT_PATH}/tfm/scripts/assemble.py \
		--layout ${PROJECT_PATH}/tfm/layout_files/signing_layout_s.o \
		-s ${BUILD_PATH}/${PROJECT_NAME}_s$* \
		-n ${BUILD_PATH}/${PROJECT_NAME}_ns$* \
		-o $@

###############################################################################
# Rule to sign SPE / secure / tf-m images
###############################################################################
${BUILD_PATH}/${PROJECT_NAME}_s_signed.bin : ${BUILD_PATH}/${PROJECT_NAME}_s.bin
	source ${TOOLS_PATH}/env_setup.sh
	python ${PROJECT_PATH}/tfm/scripts/wrapper/wrapper.py \
		--version ${SPE_VERSION} \
		--layout ${PROJECT_PATH}/tfm/layout_files/signing_layout_s.o \
		--key ${S_REGION_SIGNING_KEY} \
		--public-key-format full \
		--align 8 --pad --pad-header \
		--header-size 0x400 \
		--security-counter 1 \
		--dependencies "(1,0.0.0+0)" \
		$< \
		$@

###############################################################################
# Rule to sign NSPE / non-secure envronment images
###############################################################################
${BUILD_PATH}/${PROJECT_NAME}_ns_signed.bin : ${BUILD_PATH}/${PROJECT_NAME}_ns.bin
	source ${TOOLS_PATH}/env_setup.sh
	python ${PROJECT_PATH}/tfm/scripts/wrapper/wrapper.py \
		--version ${NSPE_VERSION} \
		--layout ${PROJECT_PATH}/tfm/layout_files/signing_layout_ns.o \
		--key ${NS_REGION_SIGNING_KEY} \
		--public-key-format full \
		--align 1 --pad --pad-header \
		--header-size 0x400 \
		--security-counter 1 \
		--dependencies "(0,${SPE_VERSION}+0)" \
		$< \
		$@

###############################################################################
# Convert signed images from bin to hex format
###############################################################################
${BUILD_PATH}/${PROJECT_NAME}_s_update.hex : ${BUILD_PATH}/${PROJECT_NAME}_s_signed.bin ${BUILD_PATH}/image_defs.sh
	source ${BUILD_PATH}/image_defs.sh
	source ${TOOLS_PATH}/env_setup.sh
	bin2hex.py --offset $$RE_IMAGE_FLASH_SECURE_UPDATE $< $@

${BUILD_PATH}/${PROJECT_NAME}_ns_update.hex : ${BUILD_PATH}/${PROJECT_NAME}_ns_signed.bin ${BUILD_PATH}/image_defs.sh
	source ${BUILD_PATH}/image_defs.sh
	source ${TOOLS_PATH}/env_setup.sh
	bin2hex.py --offset $$RE_IMAGE_FLASH_NON_SECURE_UPDATE $< $@

${BUILD_PATH}/${PROJECT_NAME}_s_signed.hex : ${BUILD_PATH}/${PROJECT_NAME}_s_signed.bin ${BUILD_PATH}/image_defs.sh
	source ${BUILD_PATH}/image_defs.sh
	source ${TOOLS_PATH}/env_setup.sh
	bin2hex.py --offset $$RE_IMAGE_FLASH_ADDRESS_SECURE $< $@

${BUILD_PATH}/${PROJECT_NAME}_ns_signed.hex : ${BUILD_PATH}/${PROJECT_NAME}_ns_signed.bin ${BUILD_PATH}/image_defs.sh
	source ${BUILD_PATH}/image_defs.sh
	source ${TOOLS_PATH}/env_setup.sh
	bin2hex.py --offset $$RE_IMAGE_FLASH_ADDRESS_NON_SECURE $< $@

${BUILD_PATH}/${PROJECT_NAME}_s_ns_signed.hex : ${BUILD_PATH}/${PROJECT_NAME}_s_signed.hex ${BUILD_PATH}/${PROJECT_NAME}_ns_signed.hex
	source ${TOOLS_PATH}/env_setup.sh
	hexmerge.py $^ -o $@

${BUILD_PATH}/${PROJECT_NAME}_s_ns_update.hex : ${BUILD_PATH}/${PROJECT_NAME}_s_update.hex ${BUILD_PATH}/${PROJECT_NAME}_ns_update.hex
	source ${TOOLS_PATH}/env_setup.sh
	hexmerge.py $^ -o $@

${BUILD_PATH}/${PROJECT_NAME}_bl2_s_ns_factory.hex : ${BUILD_PATH}/${PROJECT_NAME}_s_signed.hex ${BUILD_PATH}/${PROJECT_NAME}_ns_signed.hex ${BUILD_PATH}/${PROJECT_NAME}_bl2.hex
	source ${TOOLS_PATH}/env_setup.sh
	hexmerge.py $^ -o $@

spe_build_reqs : ${TFM_BUILD_PATH}/Makefile
spe_bin : ${BL2_BIN} ${TFM_BIN}

nspe_build_reqs : ${BL2_BIN} \
				  ${TFM_BIN} \
				  ${TFM_INTF_LIB} \
				  ${PROJECT_PATH}/tfm/interface/libtfm_s_veneers.a \
				  ${BUILD_PATH}/stm32u5xx_ns.ld \
				  ${BUILD_PATH}/image_defs.sh

all: info spe_build_reqs nspe_build_reqs spe_signed nspe_signed update factory_signed
clean: spe_clean spe_patch_libs_clean nspe_build_reqs_clean
distclean : clean tfm_distclean spe_patch_libs_distclean
