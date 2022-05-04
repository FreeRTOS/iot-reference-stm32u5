#!/bin/bash

SCRIPT_DIR=`dirname $0`
PATHTO_STM32CUBEIDE=`which stm32cubeide`

if test -z "${PATHTO_STM32CUBEIDE}"; then
    echo "ERROR: stm32cubeide must be in your path."
    exit -1
fi

STM32CUBEIDEDIR=`dirname ${PATHTO_STM32CUBEIDE}`

${STM32CUBEIDEDIR}/headless-build.sh -data ${SCRIPT_DIR}/.. -build b_u585i_iot02a_ntz/Debug