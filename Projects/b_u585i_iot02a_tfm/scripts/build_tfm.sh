#!/bin/bash
#
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
#

TFM_SRC_DIR="${PROJ_DIR}/../../Middleware/ARM/trusted-firmware-m"
TFM_BUILD_DIR="${PWD}/tfm_build"

echo "Workspace Path:   ${WORKSPACE_LOC}"
echo "Project Path:     ${PROJ_DIR}"
echo "Build Config:     ${BUILD_CONFIG}"
echo "TFM Source Path:  ${TFM_SRC_DIR}"
echo "TFM Build Path:   ${TFM_BUILD_DIR}"
echo "Mbedtls Path:     ${MBEDTLS_DIR}"
echo "Mcuboot Path:     ${MCUBOOT_DIR}"

[ -e ${WORKSPACE_LOC}/.pyenv ] || {
	echo "Setting up python environment"
	python3 -m venv ${WORKSPACE_LOC}/.pyenv || exit -1
}

echo "Activating python environment"
source ${WORKSPACE_LOC}/.pyenv/bin/activate || exit -1

echo "Installing python prerequisites"
pip3 install -r ${TFM_SRC_DIR}/tools/requirements.txt || exit -1
pip3 install -r ${TFM_SRC_DIR}/bl2/ext/mcuboot/scripts/requirements.txt || exit -1

[ -e ${MBEDTLS_DIR}/.patched ] || {
    echo "Patching mbedtls"
    git -C ${MBEDTLS_DIR} apply ${TFM_SRC_DIR}/lib/ext/mbedcrypto/*.patch

    [ $? -eq 0 ] && {
        touch ${MBEDTLS_DIR}/.patched
    }
}

[ -e ${MCUBOOT_DIR}/.patched ] || {
    echo "Patching mcuboot"
    git -C ${MCUBOOT_DIR} apply ${TFM_SRC_DIR}/lib/ext/mcuboot/*.patch

    [ $? -eq 0 ] && {
        touch ${MCUBOOT_DIR}/.patched
    }
}

[ -e ${TFM_BUILD_DIR} ] || {
    mkdir -p ${TFM_BUILD_DIR}
    cmake -S $TFM_SRC_DIR -B tfm_build -DTFM_PLATFORM=stm/b_u585i_iot02a \
        -DTFM_TOOLCHAIN_FILE=${TFM_SRC_DIR}/toolchain_GNUARM.cmake \
        -DCMAKE_BUILD_TYPE=Relwithdebinfo \
        -DMBEDCRYPTO_PATH=${MBEDTLS_DIR} \
        -DMCUBOOT_PATH=${MCUBOOT_DIR} \
        -DTFM_PROFILE=profile_large \
        -DTFM_ISOLATION_LEVEL=1 \
        -DTFM_MBEDCRYPTO_CONFIG_PATH=${TFM_SRC_DIR}/lib/ext/mbedcrypto/mbedcrypto_config/tfm_mbedcrypto_config_profile_large.h \
        -DNS=0 || exit -1
}
make -C ${TFM_BUILD_DIR} -j11 install || exit -1

# Generate the linker script for CubeIDE NS project
arm-none-eabi-gcc -E -P -xc -DBL2 -DTFM_PSA_API -I ${TFM_BUILD_DIR} -o stm32u5xx_ns.ld ${PROJ_DIR}/scripts/stm32u5xx_ns.ld.template || exit -1

# Preprocess image_macros_to_preprocess_bl2.c > image_macros_preprocessed_bl2.c
arm-none-eabi-gcc -E -P -xc -DBL2 -DTFM_PSA_API -I ${TFM_BUILD_DIR} -o ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c ${TFM_BUILD_DIR}/image_macros_to_preprocess_bl2.c || exit -1

# Copy template to build directory
cp ${PROJ_DIR}/scripts/image_defs.sh.template image_defs.sh

# Replace template variables
python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b secbootadd0 -m  RE_BL2_BOOT_ADDRESS  -d 0x80  -s 0 image_defs.sh

python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b boot -m  RE_BL2_PERSO_ADDRESS -s 0 image_defs.sh

python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b slot0 -m  RE_IMAGE_FLASH_ADDRESS_SECURE -s 0 image_defs.sh

python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b slot1 -m  RE_IMAGE_FLASH_ADDRESS_NON_SECURE -s 0 image_defs.sh

python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b slot2 -m  RE_IMAGE_FLASH_SECURE_UPDATE -s 0 image_defs.sh

python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b slot3 -m  RE_IMAGE_FLASH_NON_SECURE_UPDATE -s 0 image_defs.sh

python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b scratch -m  RE_IMAGE_FLASH_SCRATCH -s 0 image_defs.sh

python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b nvcounter -m  RE_IMAGE_FLASH_NV_COUNTERS -s 0 image_defs.sh

python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b sst -m  RE_IMAGE_FLASH_NV_PS -s 0  image_defs.sh

python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b its -m  RE_IMAGE_FLASH_NV_ITS -s 0 image_defs.sh

python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b unused -m  RE_IMAGE_FLASH_UNUSED -s 0 image_defs.sh

python3 ${TFM_BUILD_DIR}/scripts/stm_tool.py flash \
    --layout ${TFM_BUILD_DIR}/image_macros_preprocessed_bl2.c \
    -b boot -m  RE_BL2_PERSO_ADDRESS -s 0 image_defs.sh

rm -rf ${PROJ_DIR}/tfm/generated

# copy interface files
cp -r ${TFM_BUILD_DIR}/install/interface ${PROJ_DIR}/tfm/generated

# Generate static lib of veneer functions
arm-none-eabi-ar cr ${PROJ_DIR}/tfm/generated/lib/libs_veneers.a ${PROJ_DIR}/tfm/generated/lib/s_veneers.o
