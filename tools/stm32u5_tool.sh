#!/bin/bash

# Find STM32_Programmer_CLI
# shellcheck disable=SC2154
if [ -n "${cubeide_cubeprogrammer_path}" ]; then
    PROG_BIN_DIR="${cubeide_cubeprogrammer_path}"
    PROG_BIN_PATH=$(find "${cubeide_cubeprogrammer_path}" -name 'STM32_Programmer_CLI*' -type f)
    PROG_BIN=$(basename "${PROG_BIN_PATH}")
    export PATH="${PROG_BIN_DIR}:${PATH}"
elif command -v STM32_Programmer_CLI; then
    PROG_BIN="STM32_Programmer_CLI"
else
    echo "Error: STM32_Programmer_CLI could not be found."
    echo "STM32_Programmer_CLI in not in your path and the cubeide_cubeprogrammer_path environment variable was not defined."
    exit 1
fi

# Run STM32_Programmer_CLI and remove color codes.
prog_cli()
{
    echo ${PROG_BIN} --quietMode -c port=SWD "$@"
    ${PROG_BIN} --quietMode -c port=SWD "$@" | sed 's/\x1b\[[0-9;]*m//g'
}

# Locate project build directory and store it in BUILD_PATH
# Assume pwd is project main directory if not specified.
# shellcheck disable=SC2154
if [ -z "${ProjDirPath}" ]; then
    ProjDirPath="${PWD}"
fi

if [ -d "${ProjDirPath}" ]; then
    # shellcheck disable=SC2154
    if [ -n "${ConfigName}" ] && [ -d "${ProjDirPath}/${ConfigName}" ]; then
        BUILD_PATH="${ProjDirPath}/${ConfigName}"
    elif [ -d "${ProjDirPath}/Debug" ]; then
        BUILD_PATH="${ProjDirPath}/Debug"
    elif [ -d "${ProjDirPath}/Release" ]; then
        BUILD_PATH="${ProjDirPath}/Release"
    else
        BUILD_PATH="${ProjDirPath}"
    fi
fi

# Determine build artifact name
# shellcheck disable=SC2154
if [ -n "${ProjName}" ]; then
    # shellcheck disable=SC2154
    PROJECT_NAME="${ProjName}"
elif [ -n "${ProjName}" ]; then
    PROJECT_NAME="${ProjName}"
else
    PROJECT_NAME=$(basename "${PWD}")
fi

# Project settings
if [ -e "${BUILD_PATH}/image_defs.sh" ]; then
    # shellcheck disable=SC1090,SC1091
    source "${BUILD_PATH}/image_defs.sh"
    SECBOOTADD0=$(printf "0x%x" $((RE_BL2_BOOT_ADDRESS>>7)))
fi

NSBOOTADD0_DFLT=$(printf "0x%x" $((0x08000000>>7)))
FP=0x7f

check_ntz_vars() {
    echo "Project name:     ${PROJECT_NAME}"
    echo "Build path:       ${BUILD_PATH}"
    echo "Firmware binary:  ${TARGET_HEX}"
    echo "NSBOOTADD0:       ${NSBOOTADD0:=$NSBOOTADD0_DFLT}"

    if [ ! -d "${BUILD_PATH}" ]; then
        echo "Error: Failed to determine build directory path."
        echo "Please define BUILD_PATH, ProjDirPath, or run this script from the build or project directory."
        exit 1
    fi

    if [ ! -e "${BUILD_PATH}/${TARGET_HEX}" ]; then
        echo "Error: target firmware binary does not exist: ${BUILD_PATH}/${TARGET_HEX}"
        exit 1
    fi
}

check_tzen_vars() {
    echo "Project name:     ${PROJECT_NAME}"
    echo "Build path:       ${BUILD_PATH}"
    echo "Firmware binary:  ${TARGET_HEX}"
    echo "SECBOOTADD0:      ${SECBOOTADD0}"

    if [ ! -d "${BUILD_PATH}" ]; then
        echo "Error: Failed to determine build directory path."
        echo "Please define BUILD_PATH, ProjDirPath, or run this script from the build or project directory."
        exit 1
    fi

    if [ ! -e "${BUILD_PATH}/image_defs.sh" ]; then
        echo "Error: Failed to locate images_defs.sh include file."
        echo "Please build the target image and verify that your build path is correct."
        exit 1
    fi

    if [ -z "${SECBOOTADD0}" ]; then
        echo "Error: Failed to determine SECBOOTADD0 setting."
        exit 1
    fi
}

######################################################################
case "$1" in
    "flash_ntz")
        if [ -z "${TARGET_HEX}" ]; then
            TARGET_HEX="${PROJECT_NAME}.hex"
        fi

        check_ntz_vars

        echo "Setting NSBOOTADD0=${NSBOOTADD0} SWAP_BANK=0."
        prog_cli speed=fast mode=UR -ob NSBOOTADD0="${NSBOOTADD0}" SWAP_BANK=0 || {
            echo "Error: Failed to program non-trustzone firmware image."
            exit 1
        }

        sleep 1

        echo "Writing Non-TrustZone image."
        prog_cli speed=fast mode=UR -d "${BUILD_PATH}/${TARGET_HEX}" || {
            echo "Error: Failed to program non-trustzone firmware image."
            exit 1
        }
        ;;
    "flash_ns")
        if [ -z "${TARGET_HEX}" ]; then
            TARGET_HEX="${PROJECT_NAME}_ns_signed.hex"
        fi

        check_tzen_vars
        echo "Writing Non-Secure Image."
        prog_cli speed=fast mode=UR -d "${BUILD_PATH}/${TARGET_HEX}" -v || {
            echo "Error: Failed to program ${TARGET_HEX}."
            exit 1
        }
        ;;
    "flash_s")
        if [ -z "${TARGET_HEX}" ]; then
            TARGET_HEX="${PROJECT_NAME}_s_signed.hex"
        fi

        check_tzen_vars
        echo "Writing Non-Secure Image."
        prog_cli speed=fast mode=UR -d "${BUILD_PATH}/${TARGET_HEX}" -v || {
            echo "Error: Failed to program ${TARGET_HEX}."
            exit 1
        }
        ;;
    "flash_s_ns")
        if [ -z "${TARGET_HEX}" ]; then
            TARGET_HEX="${PROJECT_NAME}_s_ns_signed.hex"
        fi

        check_tzen_vars
        echo "Writing combined secure and non-secure Image."
        prog_cli speed=fast mode=UR -d "${BUILD_PATH}/${TARGET_HEX}" -v || {
            echo "Error: Failed to program ${TARGET_HEX}."
            exit 1
        }
        ;;
    "flash_ns_update")
        if [ -z "${TARGET_HEX}" ]; then
            TARGET_HEX="${PROJECT_NAME}_ns_update.hex"
        fi

        check_tzen_vars
        echo "Writing Non-Secure update Image."
        prog_cli speed=fast mode=UR -d "${BUILD_PATH}/${TARGET_HEX}" -v || {
            echo "Error: Failed to program ${TARGET_HEX}."
            exit 1
        }
        ;;
    "tz_regression")
        echo "Erasing and unlocking internal NOR flash..."
        prog_cli mode=UR -ob UNLOCK_1A=1 UNLOCK_1B=1 UNLOCK_2A=1 UNLOCK_2B=1 SECWM1_PSTRT=${FP} SECWM1_PEND=0 HDP1EN=0 HDP1_PEND=0 WRP1A_PSTRT=${FP} WRP1A_PEND=0 SECWM2_PSTRT=${FP} SECWM2_PEND=0 WRP2A_PSTRT=${FP} WRP2A_PEND=0 HDP2EN=0 HDP2_PEND=0 || {
            echo "Error: Failed to perform unlock operation"
            exit 1
        }

        echo "Setting RDP to level 1 and entering bootloader..."
        prog_cli mode=UR -ob nSWBOOT0=0 nBOOT0=0 RDP=0xDC || {
            echo "Error: Failed to set RDP=0xDC and enter bootloader."
            exit 1
        }

        echo "Regressing to RDP level 0, disabling TrustZone, and leaving bootloader."
        prog_cli mode=HOTPLUG -ob TZEN=0 RDP=0xAA nSWBOOT0=1 nBOOT0=1 || {
            echo "Error: Failed to regress to RDP level 0 and disable trustzone."
            exit 1
        }
        ;;
    "tz_enable")
        echo "Enabling TZ"
        prog_cli mode=HOTPLUG -ob TZEN=1 SRAM2_RST=0 || {
            echo "Error: Trustzone enable operation failed."
            exit 1
        }
        ;;

    "unlock")
        echo "Removing SECWM and erasing the user flash"
        prog_cli mode=UR -ob UNLOCK_1A=1 UNLOCK_1B=1 UNLOCK_2A=1 UNLOCK_2B=1 SECWM1_PSTRT=${FP} SECWM1_PEND=0 HDP1EN=0 HDP1_PEND=0 WRP1A_PSTRT=${FP} WRP1A_PEND=0 SECWM2_PSTRT=${FP} SECWM2_PEND=0 WRP2A_PSTRT=${FP} WRP2A_PEND=0 HDP2EN=0 HDP2_PEND=0 -e all || {
            echo "Error: Flash unlock operation failed."
            exit 1
        }
        ;;
    "flash_tzen_all")
        if [ -z "${TARGET_HEX}" ]; then
            TARGET_HEX="${PROJECT_NAME}_bl2_s_ns_factory.hex"
        fi

        check_tzen_vars
        echo "Enabling TZ"
        prog_cli mode=UR -ob TZEN=1 || {
            echo "Error: Trustzone enable operation failed."
            exit 1
        }

        prog_cli -hardRst || {
            echo "Error: Failed to perform hard reset."
            exit 1
        }

        echo "Removing SECWM, SWAP_BANK, and erasing the user flash"
        prog_cli mode=UR -ob UNLOCK_1A=1 UNLOCK_1B=1 UNLOCK_2A=1 UNLOCK_2B=1 SECWM1_PSTRT=${FP} SECWM1_PEND=0 HDP1EN=0 HDP1_PEND=0 WRP1A_PSTRT=${FP} WRP1A_PEND=0 SECWM2_PSTRT=${FP} SECWM2_PEND=0 WRP2A_PSTRT=${FP} WRP2A_PEND=0 HDP2EN=0 HDP2_PEND=0 SWAP_BANK=0 -e all || {
            echo "Error: Flash unlock operation failed."
            exit 1
        }

        echo "Enabling SECWM, setting SECBOOTADD0"
        prog_cli mode=UR -ob SECWM1_PSTRT=0 SECWM1_PEND=${FP} SECBOOTADD0="${SECBOOTADD0}" SRAM2_RST=0 || {
            echo "Error: Flash unlock operation failed."
            exit 1
        }

        echo "Writing all images (BL2, S, NS)"
        prog_cli speed=fast mode=UR -d "${BUILD_PATH}/${TARGET_HEX}" -v || {
            echo "Error: Failed to program ${TARGET_HEX}."
            exit 1
        }
        ;;
    "flash_tzen_update")
        if [ -z "${TARGET_HEX}" ]; then
            TARGET_HEX="${PROJECT_NAME}_s_ns_update.hex"
        fi

        prog_cli speed=fast mode=UR -d "${BUILD_PATH}/${TARGET_HEX}" -v || {
            echo "Error: Failed to program ${TARGET_HEX}."
            exit 1
        }
        ;;
    *)
        echo "Error: No valid option was specified: '$1'"
        exit 1
        ;;
esac

echo "Performing hard reset."
prog_cli -hardRst || {
    echo "Error: Failed to perform hard reset."
    exit 1
}

echo && echo "Operation Completed." && echo
