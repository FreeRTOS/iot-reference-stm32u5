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

PROG_DIR=`find ${CUBEIDE_DIR%?} -name 'com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer*' -type d`
PROG_BIN_DIR="${PROG_DIR}/tools/bin"
PROG_BIN=`find ${PROG_BIN_DIR} -name 'STM32_Programmer_CLI*' -type f`
PROG_BIN=$( basename ${PROG_BIN} )
TARGET_BIN_DIR=${CWD}

EXT_LOADER="MX25LM51245G_STM32U585I-IOT02A.stldr"
EXT_LOADER_PATH="${PROG_BIN_DIR}/ExternalLoader/${EXT_LOADER}"

[ -e "${PROG_BIN_DIR}/${PROG_BIN}" ] || {
    echo "Could not determine STM32CubeProgrammer location"
    exit -1
}

[ -e ${TARGET_BIN_DIR} ] || {
    echo "Project binary dir could not be determined."
    exit -1
}

source ${TARGET_BIN_DIR}/image_defs.sh

( [ -z "${slot0}" ] || [ -z "${slot1}" ] || [ -z "${boot}" ] ) && {
    echo "Error: Could not determine target address."
    exit -1
}

echo "STM32CubeIDE directory:               ${CUBEIDE_DIR%?}"
echo "STM32CubeProgrammer Path:             ${PROG_BIN_DIR}"
echo "STM32CubeProgrammer Bin Name:         ${PROG_BIN}"
echo; echo
echo "RE_BL2_BOOT_ADDRESS:                  ${secbootadd0}"
echo "RE_BL2_PERSO_ADDRESS:                 ${boot}"
echo "RE_IMAGE_FLASH_ADDRESS_SECURE:        ${slot0}"
echo "RE_IMAGE_FLASH_ADDRESS_NON_SECURE:    ${slot1}"
echo "RE_IMAGE_FLASH_SECURE_UPDATE:         ${slot2}"
echo "RE_IMAGE_FLASH_NON_SECURE_UPDATE:     ${slot3}"
echo

export PATH="${PROG_BIN_DIR}:${PATH}"

echo "Writing TFM Secure image"
# ${PROG_BIN} -c port=SWD mode=UR --hardRst -el ${EXT_LOADER_PATH} -d ${TARGET_BIN_DIR}/${ProjName}_s_signed.bin ${slot0} -v
${PROG_BIN} -c port=SWD mode=UR --hardRst -d ${TARGET_BIN_DIR}/${ProjName}_s_signed.bin ${slot0} -v

echo; echo
echo "Writing Non-Secure image"
# ${PROG_BIN} -c port=SWD mode=UR --hardRst -el ${EXT_LOADER_PATH} -d ${TARGET_BIN_DIR}/${ProjName}_ns_signed.bin ${slot1} -v
${PROG_BIN} -c port=SWD mode=UR --hardRst -d ${TARGET_BIN_DIR}/${ProjName}_ns_signed.bin ${slot1} -v

echo; echo
echo "Writing BL2 bootloader image"
# ${PROG_BIN} -c port=SWD mode=UR --hardRst -el ${EXT_LOADER_PATH} -d ${TARGET_BIN_DIR}/${ProjName}_bl2.bin ${boot} -v
${PROG_BIN} -c port=SWD mode=UR --hardRst -d ${TARGET_BIN_DIR}/${ProjName}_bl2.bin ${boot} -v

