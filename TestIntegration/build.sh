#!/bin/bash

SCRIPT_DIR=$(dirname "${0}")
SCRIPT_DIR=$(realpath "${SCRIPT_DIR}")
PATH_TO_STM32CUBEIDE=$(command -v stm32cubeide)

if test -z "${PATH_TO_STM32CUBEIDE}"; then
    echo "ERROR: stm32cubeide must be in your path."
    exit 255
fi

STM32CUBEIDEDIR=$(dirname "${PATH_TO_STM32CUBEIDE}")

"${STM32CUBEIDEDIR}"/headless-build.sh -data "${SCRIPT_DIR}/.." -build b_u585i_iot02a_ntz
