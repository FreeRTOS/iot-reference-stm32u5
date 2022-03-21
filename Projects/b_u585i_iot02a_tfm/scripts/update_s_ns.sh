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

source ${BUILD_PATH}/image_defs.sh

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

echo "STM32CubeIDE directory:               ${CUBEIDE_PATH%?}"
echo "STM32CubeProgrammer Path:             ${PROG_BIN_DIR}"
echo "STM32CubeProgrammer Bin Name:         ${PROG_BIN}"

export PATH="${PROG_BIN_DIR}:${PATH}"

# echo "Writing combined update image: ${PROJECT_NAME}_s_ns_update.hex"
time ${PROG_BIN} -q -c port=SWD Fast mode=Attach -d ${BUILD_PATH}/${PROJECT_NAME}_s_ns_update.hex -v || exit

# ${PROG_BIN} -q -c port=SWD Fast mode=UnderReset -d ${BUILD_PATH}/tfm_build/bin/tfm_ns_signed.bin ${RE_IMAGE_FLASH_NON_SECURE_UPDATE} -v || exit

# ${PROG_BIN} -q -c port=SWD Fast mode=UnderReset -d ${BUILD_PATH}/tfm_build/bin/tfm_s_signed.bin ${RE_IMAGE_FLASH_SECURE_UPDATE} -v || exit


# ${PROG_BIN} -q -c port=SWD Fast mode=UnderReset -d ${BUILD_PATH}/tfm_build/bin/tfm_ns_signed.bin ${RE_IMAGE_FLASH_NON_SECURE_UPDATE} -v || exit

# ${PROG_BIN} -q -c port=SWD Fast mode=UnderReset -d ${BUILD_PATH}/tfm_build/bin/tfm_s_signed.bin ${RE_IMAGE_FLASH_SECURE_UPDATE} -v || exit
