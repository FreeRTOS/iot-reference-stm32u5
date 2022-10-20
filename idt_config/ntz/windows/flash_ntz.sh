#!/bin/bash
TOOLCHAIN_PATH=$2
SOURCE_PATH=$1

export PATH=$TOOLCHAIN_PATH:$PATH
sleep 1
cd "${SOURCE_PATH}" || exit 1
source "${SOURCE_PATH}/tools/env_setup.sh"
cd "${SOURCE_PATH}/Projects/b_u585i_iot02a_ntz" || exit 1
"${SOURCE_PATH}"/tools/stm32u5_tool.sh flash_ntz
