#!/bin/bash
# NB: If the WRP, SECWM and HDP were not activated by the FW, no regression would be needed to update the secure binaries.

# # Environment settings
# if [ "$OS" = "Windows_NT" ]; then
#     PROG="/c/Progra~1/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe -q -c port=SWD"
# else
#     PROG="${HOME}/st/STM32CubeProgrammer/bin/STM32_Programmer_CLI -c port=SWD"
# fi

[ -z ${CUBEIDE_PATH} ] && {
    echo "Error: CUBEIDE_PATH must be defined to continue."
    exit 1
}

PROG_DIR=$( find ${CUBEIDE_PATH} -name 'com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer*' -type d )
PROG_BIN_DIR="${PROG_DIR}/tools/bin"
PROG_BIN=$( find ${PROG_BIN_DIR} -name 'STM32_Programmer_CLI*' -type f )
PROG_BIN=$( basename ${PROG_BIN} )

PROG="${PROG_BIN} --quietMode -c port=SWD"

echo "Project name:             ${PROJECT_NAME:="b_u585i_iot02a_tfm"}"
echo "Build path:               ${BUILD_PATH:="../Debug"}"

export PATH="${PROG_BIN_DIR}:${PATH}"

# Project settings
# source "${BUILD_PATH}/image_defs.sh"
# SECBOOTADD0=$(printf "0x%x" $((RE_BL2_BOOT_ADDRESS>>7)))
NSBOOTADD0=$(printf "0x%x" $((0x08000000>>7)))
FP=0x7f

######################################################################
case "$1" in
    "NTZ")
        echo "Writing NonTrustZone image, setting NSBOOTADD0 and clearing SWAP_BANK."
        $PROG speed=fast mode=UR -d "${BUILD_PATH}/${PROJECT_NAME}.hex" -ob NSBOOTADD0=${NSBOOTADD0} SWAP_BANK=0
        ;;
    "NS")
        echo "Writing Non-Secure Image, setting N"
        $PROG speed=fast mode=UR -d "${BUILD_PATH}/${PROJECT_NAME}_ns_signed.bin" ${RE_IMAGE_FLASH_ADDRESS_NON_SECURE} -v
        ;;
    "REG")
        echo "Regressing the chip and disabling TZ"
        $PROG mode=UR -ob UNLOCK_1A=1 UNLOCK_1B=1 UNLOCK_2A=1 UNLOCK_2B=1 SECWM1_PSTRT=${FP} SECWM1_PEND=0 HDP1EN=0 HDP1_PEND=0 WRP1A_PSTRT=${FP} WRP1A_PEND=0 SECWM2_PSTRT=${FP} SECWM2_PEND=0 WRP2A_PSTRT=${FP} WRP2A_PEND=0 HDP2EN=0 HDP2_PEND=0 -e all
        $PROG mode=UR -ob nSWBOOT0=0 nBOOT0=0 RDP=0xDC
        $PROG mode=HOTPLUG -ob TZEN=0 RDP=0xAA nSWBOOT0=1 nBOOT0=1
        ;;
    "RM")
        echo "Removing the static protections and erasing the user flash"
        $PROG mode=UR -ob UNLOCK_1A=1 UNLOCK_1B=1 UNLOCK_2A=1 UNLOCK_2B=1 SECWM1_PSTRT=${FP} SECWM1_PEND=0 HDP1EN=0 HDP1_PEND=0 WRP1A_PSTRT=${FP} WRP1A_PEND=0 SECWM2_PSTRT=${FP} SECWM2_PEND=0 WRP2A_PSTRT=${FP} WRP2A_PEND=0 HDP2EN=0 HDP2_PEND=0 -e all
        ;;
    "FULL")
        echo "Enabling SECWM, TZ, setting SECBOOTADD0"
        $PROG mode=UR -ob SECWM1_PSTRT=0 SECWM1_PEND=${FP} SECBOOTADD0=${SECBOOTADD0} TZEN=1

        echo "Writing all images (NS, S, BL2)"
        $PROG speed=fast mode=UR -d "${BUILD_PATH}/${PROJECT_NAME}_ns_signed.bin" ${RE_IMAGE_FLASH_ADDRESS_NON_SECURE} -v -d "${BUILD_PATH}/${PROJECT_NAME}_s_signed.bin" ${RE_IMAGE_FLASH_ADDRESS_SECURE} -v -d "${BUILD_PATH}/${PROJECT_NAME}_bl2.bin" ${RE_BL2_BIN_ADDRESS} -v
        ;;
    *)
        echo "No valid option was specified: '$1'"
        exit 1
        ;;
esac