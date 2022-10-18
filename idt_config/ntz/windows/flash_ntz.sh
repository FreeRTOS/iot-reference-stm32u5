#!/bin/bash
ST_IDE_PATH=$2
TOOLCHAIN_PATH=$3
SOURCE_PATH=$1

export PATH=$TOOLCHAIN_PATH:$PATH
sleep 1
cd "${SOURCE_PATH}"
source "${SOURCE_PATH}/tools/env_setup.sh"
cd "${SOURCE_PATH}/Projects/b_u585i_iot02a_ntz"
"${SOURCE_PATH}"/tools/stm32u5_tool.sh flash_ntz