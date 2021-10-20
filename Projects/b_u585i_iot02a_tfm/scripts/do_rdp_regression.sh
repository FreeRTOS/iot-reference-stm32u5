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

PROG_DIR=`find ${CUBEIDE_DIR} -name 'com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer*' -type d`
PROG_BIN_DIR="${PROG_DIR}/tools/bin"
PROG_BIN=`find ${PROG_BIN_DIR} -name 'STM32_Programmer_CLI*' -type f`
PROG_BIN=$( basename ${PROG_BIN} )
TARGET_BIN_DIR=${PWD}


[ -e "${PROG_BIN_DIR}/${PROG_BIN}" ] || {
    echo "Could not determine STM32CubeProgrammer location"
    exit -1
}

[ -e ${TARGET_BIN_DIR} ] || {
    echo "Project binary dir could not be determined."
    exit -1
}

source ${TARGET_BIN_DIR}/image_defs.sh

[ -z "${secbootadd0}" ] && {
    echo "Error: Could not determine target secbootadd0."
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

echo "Setting TZEN bit and regressing the RDP level to 0."
${PROG_BIN} -c port=SWD mode=HotPlug -ob RDP=0xAA TZEN=1

echo; echo
echo "Erasing Bank 1"
${PROG_BIN} -c port=SWD mode=UR --hardRst -ob SECWM1_PSTRT=127 SECWM1_PEND=0 WRP1A_PSTRT=127 WRP1A_PEND=0 WRP1B_PSTRT=127 WRP1B_PEND=0 -e all

echo; echo
echo "Erasing Bank 2"
${PROG_BIN} -c port=SWD mode=UR --hardRst -ob SECWM2_PSTRT=127 SECWM2_PEND=0 WRP2A_PSTRT=127 WRP2A_PEND=0 WRP2B_PSTRT=127 WRP2B_PEND=0 -e all

echo; echo
echo "Disable HDP protection"
${PROG_BIN} -c port=SWD mode=HotPlug -ob HDP1_PEND=0 HDP1EN=0 HDP2_PEND=0 HDP2EN=0

echo
echo "Setting Option Byte 1"
${PROG_BIN} -c port=SWD mode=HotPlug -ob SRAM2_RST=0 SECBOOTADD0="${secbootadd0}" DBANK=1 SWAP_BANK=0 SECWM1_PSTRT=0 SECWM1_PEND=127

echo; echo
echo "Setting Option Byte 2"
${PROG_BIN} -c port=SWD mode=HotPlug -ob SECWM2_PSTRT=0 SECWM2_PEND=127

