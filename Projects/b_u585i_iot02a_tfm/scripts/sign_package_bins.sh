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
TFM_BUILD_DIR="${PWD}/tfm_build"
TFM_INSTALL_DIR="${PWD}/tfm_build/install"
MCUBOOT_SRC_DIR="${PROJ_DIR}/../../Middleware/ARM/mcuboot"
BL2_SRC_DIR="${PROJ_DIR}/../../Middleware/ARM/trusted-firmware-m/bl2"

S_SIGNING_KEY="${BL2_SRC_DIR}/ext/mcuboot/root-RSA-3072.pem"
NS_SIGNING_KEY="${BL2_SRC_DIR}/ext/mcuboot/root-RSA-3072_1.pem"

IMG_SIGNING_DIR="${TFM_BUILD_DIR}/install/image_signing"
IMG_SCRIPTS_DIR="${IMG_SIGNING_DIR}/scripts"

echo
echo "Project Path:         ${PROJ_DIR}"
echo "TFM Build Path:       ${TFM_BUILD_DIR}"
echo "Mcuboot Source Path:  ${MCUBOOT_SRC_DIR}"
echo "BL2 Source Path:      ${BL2_SRC_DIR}"
echo "image_signing Path:   ${IMG_SIGNING_DIR}"
echo "image scripts Path:   ${IMG_SCRIPTS_DIR}"
echo "Project name:         ${BIN_NAME}"
echo "NS Version:           ${NS_VERSION}"
echo

# Enter python environment previously set up by tfm build script
source ${WORKSPACE_LOC}/.pyenv/bin/activate || exit -1

# make sure the correct version of imgtool is used
export PYTHONPATH="${MCUBOOT_SRC_DIR}/scripts:${PYTHONPATH}"

for in_file in $( ls ${BIN_NAME}.* )
do
    newname=${in_file/${BIN_NAME}/${BIN_NAME}_ns}
    echo "Renaming artifact ${in_file} to ${newname}"
    mv $in_file $newname
done

for in_file in $( ls ${TFM_BUILD_DIR}/bin/tfm_s.* )
do
    newname=$( basename ${in_file/tfm_s/${BIN_NAME}_s} )
    echo "Copying artifact ${in_file} to ${newname}"
    cp $in_file $newname
done

for in_file in $( ls ${TFM_BUILD_DIR}/bin/bl2.* )
do
    newname=$( basename ${in_file/bl2/${BIN_NAME}_bl2} )
    echo "Copying artifact ${in_file} to ${newname}"
    cp $in_file $newname
done

echo "Combining ${BIN_NAME}_s.bin and ${BIN_NAME}_ns.bin -> ${BIN_NAME}_s_ns.bin"

# Build combined s / ns image
python3 ${IMG_SCRIPTS_DIR}/assemble.py \
    --layout ${IMG_SIGNING_DIR}/layout_files/signing_layout_s.o \
    -s ${BIN_NAME}_s.bin \
    -n ${BIN_NAME}_ns.bin \
    -o ${BIN_NAME}_s_ns.bin || exit -1

# Sign s, ns images
echo "Signing ${BIN_NAME}_s.bin -> ${BIN_NAME}_s_signed.bin"

python3 ${IMG_SCRIPTS_DIR}/wrapper/wrapper.py \
    -v 1.4.0 \
    --layout ${IMG_SIGNING_DIR}/layout_files/signing_layout_s.o \
    -k ${S_SIGNING_KEY} \
    --public-key-format full \
    --align 16 --pad --pad-header -H 0x400 -s 1 -d "(1,0.0.0+0)" \
    ${BIN_NAME}_s.bin \
    ${BIN_NAME}_s_signed.bin || exit -1

echo "Signing ${BIN_NAME}_ns.bin -> ${BIN_NAME}_ns_signed.bin"
python3 ${IMG_SCRIPTS_DIR}/wrapper/wrapper.py \
    -v ${NS_VERSION} \
    --layout ${IMG_SIGNING_DIR}/layout_files/signing_layout_ns.o \
    -k ${NS_SIGNING_KEY} \
    --public-key-format full \
    --align 1 --pad --pad-header \
    -H 0x400 -s 1 -d "(0,0.0.0+0)" \
    ${BIN_NAME}_ns.bin \
    ${BIN_NAME}_ns_signed.bin || exit -1

echo "Combining ${BIN_NAME}_s_signed.bin and ${BIN_NAME}_ns_signed.bin -> ${BIN_NAME}_s_ns_signed.bin"
# Build signed combined s / ns image
python3 ${IMG_SCRIPTS_DIR}/assemble.py \
    --layout ${IMG_SIGNING_DIR}/layout_files/signing_layout_s.o \
    -s ${BIN_NAME}_s_signed.bin \
    -n ${BIN_NAME}_ns_signed.bin \
    -o ${BIN_NAME}_s_ns_signed.bin || exit -1

echo "Copying artifact ${BIN_NAME}_s_ns_signed.bin to ${BIN_NAME}.bin"
cp ${BIN_NAME}_s_ns_signed.bin ${BIN_NAME}.bin

source image_defs.sh
echo
echo "RE_BL2_BOOT_ADDRESS:                  ${secbootadd0}"
echo "RE_BL2_PERSO_ADDRESS:                 ${boot}"
echo "RE_IMAGE_FLASH_ADDRESS_SECURE:        ${slot0}"
echo "RE_IMAGE_FLASH_ADDRESS_NON_SECURE:    ${slot1}"
echo "RE_IMAGE_FLASH_SECURE_UPDATE:         ${slot2}"
echo "RE_IMAGE_FLASH_NON_SECURE_UPDATE:     ${slot3}"
echo
echo "Done"
