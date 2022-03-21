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

[ -e "${PROG_BIN_DIR}/${PROG_BIN}" ] || {
    echo "Could not determine STM32CubeProgrammer location"
    exit -1
}

echo "STM32CubeIDE directory:               ${CUBEIDE_DIR%?}"
echo "STM32CubeProgrammer Path:             ${PROG_BIN_DIR}"
echo "STM32CubeProgrammer Bin Name:         ${PROG_BIN}"

export PATH="${PROG_BIN_DIR}:${PATH}"

echo "Setting RDP to 0xAA and force boot from flash. SWAP_BANK=0"
${PROG_BIN} -q -c port=SWD mode=HotPlug -ob TZEN=1 RDP=0xAA nSWBOOT0=1 nBOOT0=1 SWAP_BANK=0
