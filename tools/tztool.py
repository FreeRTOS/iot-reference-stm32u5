#!python
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

# This user script adds the STM32U5 target to pyocd.

from tools.pyocd import target_STM32U585xx
import pyocd.target
from pyocd.core.helpers import ConnectHelper
import tools.pyocd.regmap as regmap
import argparse

def rdp_byte_to_level_str(rdp_byte):
    level = "Level 1"
    if rdp_byte == 0x55:
        level = 'Level 0.5'
    elif rdp_byte == 0xAA:
        level = 'Level 0'
    elif rdp_byte == 0xCC:
        level = 'Level 2'

    level += " (0x{:02X})".format(rdp_byte)

    return level

def bool_to_enable_disable(var):
    if var:
        return "Enabled"
    else:
        return "Disabled"

def secwm_to_str(target, bank):
    p_start = 0
    p_end = 0
    hdp_enabled = False
    hdp_pend = 0
    if bank == 1:
        hdp_enabled = bool(int(target._dev.FLASH.FLASH_SECWM1R2.HDP1EN))
        hdp_pend = int(target._dev.FLASH.FLASH_SECWM1R2.HDP1_PEND)
        p_start = int(target._dev.FLASH.FLASH_SECWM1R1.SECWM1_PSTRT)
        p_end  = int(target._dev.FLASH.FLASH_SECWM1R1.SECWM1_PEND)
    elif bank == 2:
        hdp_enabled = bool(int(target._dev.FLASH.FLASH_SECWM2R2.HDP2EN))
        hdp_pend = int(target._dev.FLASH.FLASH_SECWM2R1.SECWM2_PSTRT)
        p_start = int(target._dev.FLASH.FLASH_SECWM2R1.SECWM2_PSTRT)
        p_end  = int(target._dev.FLASH.FLASH_SECWM2R1.SECWM2_PEND)

    secwm_str = "Pages: 0x{:02X} to 0x{:02X}, Hide Protection: {}".format(p_start, p_end, bool_to_enable_disable(hdp_enabled))
    if hdp_enabled:
        secwm_str += " HDP ending Page: 0x{:02X}".format(hdp_pend)

    return secwm_str

def print_status(target):
    tz_state = target.get_trustzone_state()
    print("Trustzone:      {}".format(bool_to_enable_disable(tz_state)))
    print("Boot Address:   0x{:08X}".format(target.get_boot_addr()))
    print("RDP Level:      {}".format(rdp_byte_to_level_str(target.get_rdp_state())))
    print("SECWM Region 1: {}".format(secwm_to_str(target, 1)))
    print("SECWM Region 2: {}".format(secwm_to_str(target, 2)))


def main():
    parser = argparse.ArgumentParser(description='Display and modify trustzone related option bytes on the STM32U5 MCU.')
    parser.add_argument('action', nargs='?', choices=['enable','disable','status'], default='status' )
    parser.add_argument('-v', '--verbose', action='count', default=0)
    args = parser.parse_args()

    pyocd.target.TARGET["stm32u585xx"] = target_STM32U585xx.STM32U585xx
    pyocd.target.TARGET["stm32u585aiix"] = target_STM32U585xx.STM32U585xx

    print("Connecting to target device...\n")

    session = ConnectHelper.session_with_chosen_probe()

    session.open()
    target = session.target

    tz_state = target.get_trustzone_state()

    if args.action == 'enable' and tz_state == True:
        print("Trustzone is already enabled.\n")

    elif args.action == 'enable':
        print("Enabling Trustzone...\n")
        target.enable_trustzone()

    elif args.action == 'disable' and tz_state == False:
        print("Trustzone is already disabled.\n")

    elif args.action == 'disable':
        print("Disabling Trustzone...\n")
        target.disable_trustzone()

    print_status(target)

if __name__ == "__main__":
    main()
