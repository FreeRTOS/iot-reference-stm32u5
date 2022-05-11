#!/bin/bash

SCRIPT_DIR=$(dirname "${0}")
WORKSPACE_DIR=$(realpath "${SCRIPT_DIR}/../")

export ProjDirPath
ProjDirPath="$(realpath "${WORKSPACE_DIR}/Projects/b_u585i_iot02a_ntz")"
(cd $ProjDirPath; \
    BuildArtifactFileBaseName="b_u585i_iot02a_ntz" \
    "${WORKSPACE_DIR}"/tools/stm32u5_tool.sh flash_ntz)
