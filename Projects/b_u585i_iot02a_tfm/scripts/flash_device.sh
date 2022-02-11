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

# CUBEIDE_PATH, BUILD_PATH, and PROJECT_NAME must be provided by the caller as environment variables

[ -z ${CUBEIDE_PATH} ] && {
    echo "Error: CUBEIDE_PATH must be defined to continue."
    exit 1
}

[ -z ${BUILD_PATH} ] && {
    echo "Error: BUILD_PATH must be defined to continue."
    exit 1
}

[ -z ${PROJECT_NAME} ] && {
    echo "Error: PROJECT_NAME must be defined to continue."
    exit 1
}

PROG_DIR=$( find ${CUBEIDE_PATH} -name 'com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer*' -type d )
PROG_BIN_DIR="${PROG_DIR}/tools/bin"
PROG_BIN=$( find ${PROG_BIN_DIR} -name 'STM32_Programmer_CLI*' -type f )
PROG_BIN=$( basename ${PROG_BIN} )

EXT_LOADER="MX25LM51245G_STM32U585I-IOT02A.stldr"
EXT_LOADER_PATH="${PROG_BIN_DIR}/ExternalLoader/${EXT_LOADER}"

[ -e "${PROG_BIN_DIR}/${PROG_BIN}" ] || {
    echo "Error: Failed to determine STM32_Programmer_CLI location."
    exit 1
}

[ -e "${EXT_LOADER_PATH}" ] || {
    echo "Error: Failed to find external loader binary."
    exit 1
}

[ -e ${BUILD_PATH} ] || {
    echo "Error: Project build directory must be defined in the BUILD_PATH environment variable."
    exit 1
}

source ${BUILD_PATH}/image_defs.sh

# Valdate required items from image_defs.sh
( [ -z "${RE_IMAGE_FLASH_ADDRESS_SECURE}" ] || [ -z "${RE_IMAGE_FLASH_ADDRESS_NON_SECURE}" ] || [ -z "${RE_BL2_BOOT_ADDRESS}" ] ) && {
    echo "Error: Could not determine target address."
    exit 1
}

SECBOOTADD0=$(printf "0x%x" $((RE_BL2_BOOT_ADDRESS>>7)))

echo "STM32CubeIDE directory:               ${CUBEIDE_PATH%?}"
echo "STM32CubeProgrammer Path:             ${PROG_BIN_DIR}"
echo "STM32CubeProgrammer Bin Name:         ${PROG_BIN}"
echo; echo
echo "RE_BL2_BOOT_ADDRESS:                  ${RE_BL2_BOOT_ADDRESS}"
echo ">> 7 = SECBOOTADD0:                   ${SECBOOTADD0}"
echo "RE_IMAGE_FLASH_ADDRESS_SECURE:        ${RE_IMAGE_FLASH_ADDRESS_SECURE}"
echo "RE_IMAGE_FLASH_ADDRESS_NON_SECURE:    ${RE_IMAGE_FLASH_ADDRESS_NON_SECURE}"
echo

export PATH="${PROG_BIN_DIR}:${PATH}"

echo "Setting TZEN bit and regressing the RDP level to 0."
${PROG_BIN} -c port=SWD mode=HotPlug -ob RDP=0xAA TZEN=1 || exit

echo "Erasing Internal NOR Flash Bank 1"
${PROG_BIN} -q -c port=SWD mode=HotPlug --hardRst -ob SECWM1_PSTRT=127 SECWM1_PEND=0 WRP1A_PSTRT=127 WRP1A_PEND=0 WRP1B_PSTRT=127 WRP1B_PEND=0 -e all || exit

echo "Erasing Internal NOR Flash Bank 2"
${PROG_BIN} -q -c port=SWD mode=HotPlug -ob SECWM2_PSTRT=127 SECWM2_PEND=0 WRP2A_PSTRT=127 WRP2A_PEND=0 WRP2B_PSTRT=127 WRP2B_PEND=0 -e all || exit

echo "Disabling Internal NOR flash protection (HDP)"
${PROG_BIN} -q -c port=SWD mode=HotPlug -ob HDP1_PEND=0 HDP1EN=0 HDP2_PEND=0 HDP2EN=0 || exit

echo "Setting SECBOOTADD0 option bytes"
${PROG_BIN} -q -c port=SWD mode=HotPlug -ob SECBOOTADD0=${SECBOOTADD0} || exit

echo "Writing Secure Image: ${PROJECT_NAME}_s_signed.bin to Address: ${RE_IMAGE_FLASH_ADDRESS_SECURE}"
${PROG_BIN} -q -c port=SWD Fast mode=HotPlug -el ${EXT_LOADER_PATH} -d ${BUILD_PATH}/${PROJECT_NAME}_s_signed.bin ${RE_IMAGE_FLASH_ADDRESS_SECURE} -v || exit

echo; echo
echo "Writing Non-Secure Image: ${PROJECT_NAME}_ns_signed.bin to Address: ${RE_IMAGE_FLASH_ADDRESS_NON_SECURE}"
${PROG_BIN} -q -c port=SWD Fast mode=HotPlug -el ${EXT_LOADER_PATH} -d ${BUILD_PATH}/${PROJECT_NAME}_ns_signed.bin ${RE_IMAGE_FLASH_ADDRESS_NON_SECURE} -v || exit

echo; echo
echo "Writing BL2 Image: ${PROJECT_NAME}_bl2.bin to Address: ${RE_BL2_BIN_ADDRESS}"
${PROG_BIN} -q -c port=SWD Fast mode=HotPlug -el ${EXT_LOADER_PATH} -d ${BUILD_PATH}/${PROJECT_NAME}_bl2.bin ${RE_BL2_BIN_ADDRESS} -v || exit

${PROG_BIN} -c port=SWD Fast mode=HotPlug --hardRst -ob SECWM1_PSTRT=0 SECWM1_PEND=127 -v || exit
