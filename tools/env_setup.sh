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

VENV_PATH=$(realpath "${WORKSPACE_PATH}"/.venv)
[ -n "${DEBUG}" ] && echo "VENV_PATH:          ${VENV_PATH}"

MCUBOOT_SCRIPTS=$(realpath "${WORKSPACE_PATH}"/Middleware/ARM/mcuboot/scripts)
[ -n "${DEBUG}" ] && echo "MCUBOOT_SCRIPTS:    ${MCUBOOT_SCRIPTS}"
[ -n "${DEBUG}" ] && echo

pycmd="python"

if command -v python > /dev/null 2>&1; then
    pycmd="python"
elif command -v python3 > /dev/null 2>&1; then
    pycmd="python3"
fi

env_init() {
    command -v "${pycmd}" > /dev/null 2>&1 || {
        echo "Error: Failed to find a valid python installation."
        return 1
    }

    if command -v winpty > /dev/null 2>&1; then
        # Determine if shell is interactive
        if tty -s; then
            pycmd="winpty ${pycmd}"
        fi
    fi

    unset VIRTUAL_ENV
    VIRTUAL_ENV="${VENV_PATH}"

    OSTYPE=$(uname -o 2> /dev/null | tr '[:upper:]' '[:lower:]')

    echo "OSTYPE: ${OSTYPE}"

    [ -n "${DEBUG}" ] && echo "OSTYPE: ${OSTYPE}"

    CYGPATH_ARG=''
    if [ "$OSTYPE" = "cygwin" ];then
        CYGPATH_ARG=''
    elif [ "$OSTYPE" = "msys" ]; then
        CYGPATH_ARG='--unix'
    elif [ "$OSTYPE" = "ms/windows" ]; then
        CYGPATH_ARG='--mixed'
    fi

    if [ -n "${CYGPATH_ARG}" ]; then
        if command -v cygpath &> /dev/null; then
            VIRTUAL_ENV=$(cygpath "${CYGPATH_ARG}" "${VIRTUAL_ENV}")
        fi
    fi

    if [ ! -d "${VENV_PATH}" ]; then
        if ! ${pycmd} -m virtualenv --version > /dev/null 2>&1; then
            echo "Attempting to install the virtualenv package with pip."
            ${pycmd} -m pip install --user virtualenv
        fi
        echo "Setting up python virtual environment"
        ${pycmd} -m virtualenv "${VENV_PATH}" || {
            return 1
            echo "Virtualenv initialization failed"
        }
    fi

    [ -n "${DEBUG}" ] && echo "Activating vritual environment."


    if [ -e "${VENV_PATH}/Scripts" ]; then
        VIRTUALENV_SHIM_DIR="${VENV_PATH}/Scripts"
    elif [ -e "${VENV_PATH}/bin" ]; then
        VIRTUALENV_SHIM_DIR="${VENV_PATH}/bin"
    else
        echo "Error. Could not find python virtual environment shim directory."
        return 1
    fi

    if echo "${PATH}" | grep -qF ';' -; then
        PATH_SEPARATOR=';'
    elif echo "${PATH}" | grep -qF ':' -; then
        PATH_SEPARATOR=':'
    else
        echo "Failed to determine path separator from ${PATH}."
        return 1
    fi

    SITE_PACKAGES_PATH=$(find "${VENV_PATH}" -name site-packages -type d)

    if [ -n "${CYGPATH_ARG}" ]; then
        if command -v cygpath &> /dev/null; then
            SITE_PACKAGES_PATH=$(cygpath "${CYGPATH_ARG}" "${SITE_PACKAGES_PATH}")
            TOOLS_PATH=$(cygpath "${CYGPATH_ARG}" "${TOOLS_PATH}")
            MCUBOOT_SCRIPTS=$(cygpath "${CYGPATH_ARG}" "${MCUBOOT_SCRIPTS}")
            VIRTUAL_ENV=$(cygpath "${CYGPATH_ARG}" "${VIRTUAL_ENV}")
            VIRTUALENV_SHIM_DIR=$(cygpath "${CYGPATH_ARG}" "${VIRTUALENV_SHIM_DIR}")
        fi
    fi

    [ -n "${DEBUG}" ] && echo "SITE_PACKAGES_PATH: ${SITE_PACKAGES_PATH}"

    # Use DEFAULT_PATH as the starting PATH if available
    if [ -n "${DEFAULT_PATH}" ]; then
        PATH="${DEFAULT_PATH}"
    else
        # Otherwise export the current PATH into DEFAULT_PATH
        DEFAULT_PATH="${PATH}"
    fi

    export DEFAULT_PATH

    PATH="${SITE_PACKAGES_PATH}${PATH_SEPARATOR}${PATH}"
    PATH="${VIRTUALENV_SHIM_DIR}${PATH_SEPARATOR}${PATH}"
    PATH="${MCUBOOT_SCRIPTS}${PATH_SEPARATOR}${PATH}"
    PATH="${TOOLS_PATH}${PATH_SEPARATOR}${PATH}"

    export VIRTUAL_ENV
    export PATH
    unset PYTHONPATH
    unset PYTHONHOME

    # .initialized has not been written or requirements.txt / env_setup.sh have been modified
    if [ ! -e "${VENV_PATH}"/.initialized ] || \
    [ "${TOOLS_PATH}/requirements.txt" -nt "${VENV_PATH}/.initialized" ] || \
    [ "${TOOLS_PATH}/env_setup.sh" -nt "${VENV_PATH}/.initialized" ]; then
        ( ${pycmd} -m pip install --require-virtualenv --upgrade pip &&
          ${pycmd} -m pip install --require-virtualenv -r "${TOOLS_PATH}"/requirements.txt ) || {
            rm -rf "${VENV_PATH}"
            echo "Error while installing requried python packages. Removing incomplete virtual environment."
            return 1
        }

        touch "${VENV_PATH}"/.initialized
    fi
}

env_init

# shellcheck disable=SC2139
alias python3="${pycmd}"
# shellcheck disable=SC2139
alias python="${pycmd}"

hash -r 2>/dev/null
