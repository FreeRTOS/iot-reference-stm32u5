#!/bin/bash


SCRIPT_DIR=`dirname $0`

(cd ${SCRIPT_DIR}/../Projects/b_u585i_iot02a_ntz; \
ProjDirPath=Debug \
../../tools/stm32u5_tool.sh flash_ntz)
