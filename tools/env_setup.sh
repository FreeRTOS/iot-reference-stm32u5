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

[ -n "${WORKSPACE_PATH}" ] || {
	echo "WORKSPACE_PATH env variable was not defined. Defaulting to PWD."
	WORKSPACE_PATH="${PWD}"
}

TOOLS_PATH="${WORKSPACE_PATH}/tools"

[ -n "${DEBUG}" ] && echo "TOOLS_PATH:         ${TOOLS_PATH}"

VENV_PATH=$(realpath "${TOOLS_PATH}/..")/.venv
[ -n "${DEBUG}" ] && echo "VENV_PATH:          ${VENV_PATH}"

MCUBOOT_PATH=$(realpath "${TOOLS_PATH}/..")/Middleware/ARM/mcuboot
[ -n "${DEBUG}" ] && echo "MCUBOOT_PATH:       ${MCUBOOT_PATH}"
[ -n "${DEBUG}" ] && echo

command -v python > /dev/null 2>&1 || alias python=python3
command -v python > /dev/null 2>&1 || {
	echo "Error: Failed to find a valid python installation."
	return 1
}

load_venv()
{
	if [ -e "${VENV_PATH}"/Scripts ]; then
		# shellcheck disable=SC1091
		source "${VENV_PATH}"/Scripts/activate
	elif [ -e "${VENV_PATH}"/bin ]; then
		# shellcheck disable=SC1091
		source "${VENV_PATH}"/bin/activate
	else
		echo "Error. Could not find python virtual environment activation script."
		return 1
	fi
}

# Find location of STM32_Programmer_CLI
if command -v STM32_Programmer_CLI > /dev/null 2>&1; then
	cli_path=$(command -v STM32_Programmer_CLI)
	echo "Found STM32_Programmer_CLI in PATH at ${cli_path} ."
elif [ -n "${cubeide_cubeprogrammer_path}" ] && [ -e "${cubeide_cubeprogrammer_path}/STM32_Programmer_CLI" ]; then
		PATH="${cubeide_cubeprogrammer_path}:${PATH}"
		echo "Added ${cubeide_cubeprogrammer_path} to PATH for STM32_Programmer_CLI."
else
	case "${OSTYPE}" in
		darwin*)  search_path=(/Applications/STM32CubeIDE*) ;;
		linux*)   search_path=(/opt/st/stm32cubeide*) ;;
		msys*)	  search_path=(/c/ST/STM32CubeIDE*) ;;
		cygwin*)  search_path=(/c/ST/STM32CubeIDE*) ;;
		*)        search_path=(); echo "Unknown OSTYPE=${OSTYPE}" ;;
	esac

	if [ -z "${search_path[*]}" ]; then
		echo "Error: Failed to determine search path for STM32_Programmer_CLI."
	else
		# Word-splitting is intentional.
		# shellcheck disable=SC2086
		cli_path=$(find ${search_path[*]} -name "STM32_Programmer_CLI*" -type f -exec ls -1t {} + | head -1)
		cli_dir=$(dirname "${cli_path}")
		if [ -e "${cli_path}" ] && [ -d "${cli_dir}" ]; then
			export PATH="${cli_dir}:${PATH}"
			echo "Added ${cli_dir} to PATH for STM32_Programmer_CLI."
		fi
	fi
fi

if [ ! -d "${VENV_PATH}" ]; then
	echo "Setting up python virtual environment"
	python -m virtualenv "${VENV_PATH}" || return 1
fi

[ -n "${DEBUG}" ] && echo "Activating vritual environment."
load_venv || return 1

# .initialized has not been written or requirements.txt / env_setup.sh have been modified
if [[ ! -e "${VENV_PATH}"/.initialized ]] || \
   [[ "${TOOLS_PATH}/requirements.txt" -nt "${VENV_PATH}/.initialized" ]] || \
   [[ "${TOOLS_PATH}/env_setup.sh" -nt "${VENV_PATH}/.initialized" ]]; then
	python -m pip install -r "${TOOLS_PATH}"/requirements.txt || {
		rm -rf "${VENV_PATH}"
		echo "Error while installing requried python packages. Removing incomplete virtual environment."
		return 1
	}

	SITE_PACKAGES_PATH=$(find "${VENV_PATH}" -name site-packages -type d)
	echo "SITE_PACKAGES_PATH: ${SITE_PACKAGES_PATH}"

	echo "Adding workspace directory to package path."
	echo "${WORKSPACE_PATH}" > "${SITE_PACKAGES_PATH}"/workspace.pth

	echo "Adding tools directory to package path."
	echo "${TOOLS_PATH}" > "${SITE_PACKAGES_PATH}"/tools.pth

	echo "Adding mcuboot scripts directory to package path."
	echo "${MCUBOOT_PATH}"/scripts > "${SITE_PACKAGES_PATH}"/mcuboot.pth

	touch "${VENV_PATH}"/.initialized
fi

SITE_PACKAGES_PATH=$(find "${VENV_PATH}" -name site-packages -type d)

# Setup PATH and PYTHONPATH for cmake
[ -n "${DEBUG}" ] && echo "Adding tools and site packages to PATH."
export PATH="${TOOLS_PATH}:${SITE_PACKAGES_PATH}:${PATH}"

[ -n "${DEBUG}" ] && echo "Adding tools and site packages to PYTHONPATH."
export PYTHONPATH="${TOOLS_PATH}:${SITE_PACKAGES_PATH}:${PYTHONPATH}"

[ -n "${DEBUG}" ] && echo "Done."

[ -n "${DEBUG}" ] && exit 0 || return 0
