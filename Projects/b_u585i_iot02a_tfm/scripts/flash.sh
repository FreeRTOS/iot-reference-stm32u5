#!/bin/bash

TFM_BUILD_DIR="${PWD}/tfm_build"
PROG_DIR=`find ${CUBEIDE_DIR} -name 'com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer*' -type d`
PROG_BIN=$( echo ${PROGRAMMER_DIR}/tools/bin/STM32_Programmer_CLI* )
echo "CubeIDE directory: 	${CUBEIDE_DIR}"
echo "Programmer DIR: 		${PROG_DIR}"
echo "Programmer BIN: 		${PROG_BIN}"

source image_defs.sh

echo
echo "RE_BL2_BOOT_ADDRESS:                  ${secbootadd0}"
echo "RE_BL2_PERSO_ADDRESS:                 ${boot}"
echo "RE_IMAGE_FLASH_ADDRESS_SECURE:        ${slot0}"
echo "RE_IMAGE_FLASH_ADDRESS_NON_SECURE:    ${slot1}"
echo "RE_IMAGE_FLASH_SECURE_UPDATE:         ${slot2}"
echo "RE_IMAGE_FLASH_NON_SECURE_UPDATE:     ${slot3}"
echo

