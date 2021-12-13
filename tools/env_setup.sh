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
if [ -n "${BASH_SOURCE[0]}" ]; then
	TOOLS_PATH=$( realpath "$(cd "$(dirname "${BASH_SOURCE[0]}")" || return; pwd )" )
else
	TOOLS_PATH=$( realpath "$(cd "$(dirname "${0}")" || return; pwd )" )
fi
echo "TOOLS_PATH: ${TOOLS_PATH}"

VENV_PATH=$(realpath "${TOOLS_PATH}/..")/.venv
echo "VENV_PATH:  ${VENV_PATH}"
echo

which python > /dev/null 2>&1 || alias python=python3
which python > /dev/null 2>&1 || {
	echo "Failed to find a validate python installation."
	return 1
}

load_venv()
{
	if [ -e "${VENV_PATH}"/Scripts ]; then
		source "${VENV_PATH}"/Scripts/activate
	elif [ -e "${VENV_PATH}"/bin ]; then
		source "${VENV_PATH}"/bin/activate
	else
		echo "Error. Could not find python virtual environment activation script."
		return 1
	fi

	echo "Adding tools directory to path."
	export PATH="${TOOLS_PATH}:${PATH}"
}

if [ ! -d "${VENV_PATH}" ]; then
	echo "Setting up python virtual environment"
	python -m virtualenv "${VENV_PATH}" || return 1

	echo "Activating vritual environment."
	load_venv || return 1

	pip3 install -r "${TOOLS_PATH}"/requirements.txt || {
		rm -rf "${VENV_PATH}"
		echo "Error while installing requried python packages. Removing incomplete virtual environment."
		return 1
	}
else
	load_venv || return 1
fi
echo "Done."
