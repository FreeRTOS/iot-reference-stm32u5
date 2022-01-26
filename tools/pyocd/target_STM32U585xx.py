# pyOCD debugger
# Copyright (C) 2021 Amazon.com, Inc. or its affiliates.
# Copyright (c) 2021 ARM Ltd.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import time
import logging
import os
from pyocd.coresight.coresight_target import CoreSightTarget
from pyocd.core.memory_map import (FlashRegion, RamRegion, RomRegion, MemoryMap, DeviceRegion)
from pyocd.coresight.cortex_m import CortexM
from pyocd.debug.svd.loader import SVDFile
from pyocd.flash.flash import Flash
import tools.pyocd.regmap as regmap
import pyocd

LOG = logging.getLogger(__name__)

# From ARM provided DFP pack, STM32U5xx_2M_0800.FLM
FLASH_ALGO_2M_NTZ = {
    'load_address' : 0x20000000,

    # Flash algorithm as a hex string
    'instructions': [
    0xe00abe00,
    0x8f4ff3bf, 0x49fe4770, 0xf3c16c09, 0x47705040, 0x6c0949fb, 0x47700fc8, 0xf7ffb510, 0x2801fff9,
    0xf7ffd108, 0x2801fff0, 0x0344d101, 0xf44fe00a, 0xe0074480, 0xffe7f7ff, 0xd1012801, 0xe0010344,
    0x4480f44f, 0xbd104620, 0x4604b570, 0x4616460d, 0xffdef7ff, 0x48eab988, 0xf0006a80, 0xb1204000,
    0x49e748e8, 0x48e86088, 0xbf006088, 0x6a0048e4, 0x3080f400, 0xd1f92800, 0xf7ffe03e, 0x2801ffc4,
    0x1e80d121, 0x318049de, 0x49dd6008, 0x0084f8c1, 0x318849db, 0x1d096008, 0x49d96008, 0x600831a0,
    0x60081d09, 0x60081d09, 0x60081d09, 0x49d448d5, 0x48d560c8, 0xbf0060c8, 0x6a4048d1, 0x3080f400,
    0xd1f92800, 0xf04fe018, 0x49cd30ff, 0x60083180, 0xf8c149cb, 0x49ca0084, 0x60083188, 0x60081d09,
    0x49c748c8, 0x48c860c8, 0xbf0060c8, 0x6a4048c4, 0x3080f400, 0xd1f92800, 0x6c0048c1, 0x28aab2c0,
    0x48c2d021, 0x610849be, 0x610848c1, 0x6c004608, 0x00fff020, 0x46086408, 0xf0406c00, 0x640800aa,
    0x6a804608, 0x3080f440, 0x46086288, 0xf0406a80, 0x62886000, 0xff64f7ff, 0x48b1bf00, 0xf0006a80,
    0x28006000, 0xbf00d1f9, 0x6a0048ad, 0x3080f400, 0xd1f92800, 0x444848af, 0x48af6004, 0x04008800,
    0x49ae0980, 0x60084449, 0x68004608, 0x49ac0840, 0x60084449, 0xff50f7ff, 0x444949aa, 0x20006008,
    0xb510bd70, 0xf7ff4604, 0xb930ff43, 0x6a80489c, 0x4000f040, 0x6288499a, 0x4899e005, 0xf0406ac0,
    0x49974000, 0xf7ff62c8, 0x2000ff2b, 0xb510bd10, 0xff2ef7ff, 0xbf00bb50, 0x6a404891, 0x3080f400,
    0xd1f92800, 0x30faf244, 0x6208498d, 0x6a804608, 0x430813c9, 0x6288498a, 0x6a804608, 0x3080f440,
    0xf7ff6288, 0xbf00ff0d, 0x6a004885, 0x3080f400, 0xd1f92800, 0x6a804882, 0x0004f020, 0x62884980,
    0x6a804608, 0x4000f420, 0xe0256288, 0x487cbf00, 0xf4006a40, 0x28003080, 0xf244d1f9, 0x497830fa,
    0x13c86248, 0x460862c8, 0xf4406ac0, 0x62c83080, 0xfee6f7ff, 0x4872bf00, 0xf4006a40, 0x28003080,
    0x486fd1f9, 0xf0206a80, 0x496d0004, 0x46086288, 0xf4206a80, 0x62884000, 0xbd102000, 0x4604b570,
    0x6f40f1b4, 0xf1a4d301, 0xf04f6480, 0xf7ff6601, 0x2800fecf, 0x4862d13e, 0xf0406a80, 0x49600002,
    0x42b46288, 0x0b65d309, 0xf0053d80, 0x4608057f, 0xf4406a80, 0x62886000, 0xf3c4e007, 0x48583546,
    0xf4206a80, 0x49566000, 0x48556288, 0xf4206a80, 0x495360ff, 0x46086288, 0xea406a80, 0x628800c5,
    0x6a804608, 0x3080f440, 0xbf006288, 0x6a00484c, 0x3080f400, 0xd1f92800, 0x6a004849, 0x0002f000,
    0x2001b108, 0x4846bd70, 0xf0206a80, 0x49440002, 0xe03d6288, 0x6ac04842, 0x0002f040, 0x62c84940,
    0xd30942b4, 0x3d800b65, 0x057ff005, 0x6ac04608, 0x6000f440, 0xe00762c8, 0x3546f3c4, 0x6ac04838,
    0x6000f420, 0x62c84936, 0x6ac04835, 0x60fff420, 0x62c84933, 0x6ac04608, 0x00c5ea40, 0x460862c8,
    0xf4406ac0, 0x62c83080, 0x482dbf00, 0xf4006a40, 0x28003080, 0x482ad1f9, 0xf0006a40, 0xb1080002,
    0xe7bf2001, 0x6ac04826, 0x0002f020, 0x62c84924, 0xe7b72000, 0x47fce92d, 0x460f4605, 0xf0054690,
    0xf04f0607, 0xf1070a00, 0xf020000f, 0xf244070f, 0x491b30fa, 0xf7ff6208, 0x2800fe3b, 0x2001d174,
    0x62884917, 0x2f08e099, 0xf240d361, 0x491410ff, 0x2e006208, 0xeba5d047, 0x24000a06, 0xf89ae006,
    0xf80d0000, 0xf10a0004, 0x1c640a01, 0xd3f642b4, 0xe0072400, 0x0000f898, 0xf80d19a1, 0xf1080001,
    0x1c640801, 0x0008f1c6, 0xd8f342a0, 0x0a06eba5, 0x4803bf00, 0xf4006a00, 0x28003080, 0xe013d1f9,
    0x40022000, 0x45670123, 0xcdef89ab, 0x08192a3b, 0x4c5d6e7f, 0x00000004, 0x0bfa05e0, 0x00000008,
    0x0000000c, 0x00000010, 0xf8ca9800, 0x98010000, 0x0004f8ca, 0xbf00bf00, 0x0008f1c6, 0xf1c61a3f,
    0x44050008, 0xbf002600, 0x6a00486e, 0x3080f400, 0xd1f92800, 0x0000f8d8, 0xf8d86028, 0x60680004,
    0x0808f108, 0x35083f08, 0xbf00bf00, 0x2400e022, 0xf898e006, 0xf80d0000, 0xf1080004, 0x1c640801,
    0xd3f642bc, 0xe0052400, 0x20ffe02e, 0xf80d19e1, 0x1c640001, 0x0008f1c7, 0xd8f642a0, 0x4859bf00,
    0xf4006a00, 0x28003080, 0x9800d1f9, 0x98016028, 0x27006068, 0x4853bf00, 0xf4006a00, 0x28003080,
    0x4850d1f9, 0xf2446a00, 0x400831fa, 0x4608b128, 0x6208494c, 0xe8bd2001, 0x2f0087fc, 0xaf63f47f,
    0x49482000, 0xe08b6288, 0x49462001, 0xe08162c8, 0xd34b2f08, 0x10fff240, 0x62484942, 0xeba5b396,
    0x24000a06, 0xf89ae006, 0xf80d0000, 0xf10a0004, 0x1c640a01, 0xd3f642b4, 0xe0072400, 0x0000f898,
    0xf80d19a1, 0xf1080001, 0x1c640801, 0x0008f1c6, 0xd8f342a0, 0x0a06eba5, 0x4832bf00, 0xf4006a40,
    0x28003080, 0x9800d1f9, 0x0000f8ca, 0xf8ca9801, 0xbf000004, 0xf1c6bf00, 0x1a3f0008, 0x0008f1c6,
    0x26004405, 0x4827bf00, 0xf4006a40, 0x28003080, 0xf8d8d1f9, 0x60280000, 0x0004f8d8, 0xf1086068,
    0x3f080808, 0xbf003508, 0xe021bf00, 0xe0062400, 0x0000f898, 0x0004f80d, 0x0801f108, 0x42bc1c64,
    0x2400d3f6, 0x20ffe004, 0xf80d19e1, 0x1c640001, 0x0008f1c7, 0xd8f642a0, 0x4812bf00, 0xf4006a40,
    0x28003080, 0x9800d1f9, 0x98016028, 0x27006068, 0x480cbf00, 0xf4006a40, 0x28003080, 0x4809d1f9,
    0xf2446a40, 0x400831fa, 0x4608b120, 0x62484905, 0xe7702001, 0xf47f2f00, 0x2000af7b, 0x62c84901,
    0xe7682000, 0x40022000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    ],

    # Relative function addresses
    'pc_init': 0x2000004d,
    'pc_unInit': 0x20000187,
    'pc_program_page': 0x20000379,
    'pc_erase_sector': 0x20000261,
    'pc_eraseAll': 0x200001b3,

    'static_base' : 0x20000000 + 0x00000004 + 0x00000608,
    'begin_stack' : 0x20000820,
    'begin_data' : 0x20000000 + 0x1000,
    'page_size' : 0x400,
    'analyzer_supported' : False,
    'analyzer_address' : 0x00000000,
    'page_buffers' : [0x20001000, 0x20001400],   # Enable double buffering
    'min_program_length' : 0x400,

    # Relative region addresses and sizes
    'ro_start': 0x0,
    'ro_size': 0x608,
    'rw_start': 0x608,
    'rw_size': 0x14,
    'zi_start': 0x61c,
    'zi_size': 0x0,

    # Flash information
    'flash_start': 0x8000000,
    'flash_size': 0x200000,
    'sector_sizes': (
        (0x0, 0x2000),
    )
}

# From ARM provided DFP pack, STM32U5xx_2M_0C00.FLM
FLASH_ALGO_2M_TZEN = {
    'load_address' : 0x20000000,

    # Flash algorithm as a hex string
    'instructions': [
    0xe00abe00,
    0x8f4ff3bf, 0x49fe4770, 0xf3c16c09, 0x47705040, 0x6c0949fb, 0x47700fc8, 0xf7ffb510, 0x2801fff9,
    0xf7ffd108, 0x2801fff0, 0x0344d101, 0xf44fe00a, 0xe0074480, 0xffe7f7ff, 0xd1012801, 0xe0010344,
    0x4480f44f, 0xbd104620, 0x4604b570, 0x4616460d, 0xffdef7ff, 0x48eab988, 0xf0006a80, 0xb1204000,
    0x49e748e8, 0x48e86088, 0xbf006088, 0x6a0048e4, 0x3080f400, 0xd1f92800, 0xf7ffe03e, 0x2801ffc4,
    0x1e80d121, 0x318049de, 0x49dd6008, 0x0084f8c1, 0x318849db, 0x1d096008, 0x49d96008, 0x600831a0,
    0x60081d09, 0x60081d09, 0x60081d09, 0x49d448d5, 0x48d560c8, 0xbf0060c8, 0x6a4048d1, 0x3080f400,
    0xd1f92800, 0xf04fe018, 0x49cd30ff, 0x60083180, 0xf8c149cb, 0x49ca0084, 0x60083188, 0x60081d09,
    0x49c748c8, 0x48c860c8, 0xbf0060c8, 0x6a4048c4, 0x3080f400, 0xd1f92800, 0x6c0048c1, 0x28aab2c0,
    0x48c2d021, 0x610849be, 0x610848c1, 0x6c004608, 0x00fff020, 0x46086408, 0xf0406c00, 0x640800aa,
    0x6a804608, 0x3080f440, 0x46086288, 0xf0406a80, 0x62886000, 0xff64f7ff, 0x48b1bf00, 0xf0006a80,
    0x28006000, 0xbf00d1f9, 0x6a0048ad, 0x3080f400, 0xd1f92800, 0x444848af, 0x48af6004, 0x04008800,
    0x49ae0980, 0x60084449, 0x68004608, 0x49ac0840, 0x60084449, 0xff50f7ff, 0x444949aa, 0x20006008,
    0xb510bd70, 0xf7ff4604, 0xb930ff43, 0x6a80489c, 0x4000f040, 0x6288499a, 0x4899e005, 0xf0406ac0,
    0x49974000, 0xf7ff62c8, 0x2000ff2b, 0xb510bd10, 0xff2ef7ff, 0xbf00bb50, 0x6a404891, 0x3080f400,
    0xd1f92800, 0x30faf244, 0x6208498d, 0x6a804608, 0x430813c9, 0x6288498a, 0x6a804608, 0x3080f440,
    0xf7ff6288, 0xbf00ff0d, 0x6a004885, 0x3080f400, 0xd1f92800, 0x6a804882, 0x0004f020, 0x62884980,
    0x6a804608, 0x4000f420, 0xe0256288, 0x487cbf00, 0xf4006a40, 0x28003080, 0xf244d1f9, 0x497830fa,
    0x13c86248, 0x460862c8, 0xf4406ac0, 0x62c83080, 0xfee6f7ff, 0x4872bf00, 0xf4006a40, 0x28003080,
    0x486fd1f9, 0xf0206a80, 0x496d0004, 0x46086288, 0xf4206a80, 0x62884000, 0xbd102000, 0x4604b570,
    0x6f40f1b4, 0xf1a4d301, 0xf04f6480, 0xf7ff6601, 0x2800fecf, 0x4862d13e, 0xf0406a80, 0x49600002,
    0x42b46288, 0x0b65d309, 0xf0053d80, 0x4608057f, 0xf4406a80, 0x62886000, 0xf3c4e007, 0x48583546,
    0xf4206a80, 0x49566000, 0x48556288, 0xf4206a80, 0x495360ff, 0x46086288, 0xea406a80, 0x628800c5,
    0x6a804608, 0x3080f440, 0xbf006288, 0x6a00484c, 0x3080f400, 0xd1f92800, 0x6a004849, 0x0002f000,
    0x2001b108, 0x4846bd70, 0xf0206a80, 0x49440002, 0xe03d6288, 0x6ac04842, 0x0002f040, 0x62c84940,
    0xd30942b4, 0x3d800b65, 0x057ff005, 0x6ac04608, 0x6000f440, 0xe00762c8, 0x3546f3c4, 0x6ac04838,
    0x6000f420, 0x62c84936, 0x6ac04835, 0x60fff420, 0x62c84933, 0x6ac04608, 0x00c5ea40, 0x460862c8,
    0xf4406ac0, 0x62c83080, 0x482dbf00, 0xf4006a40, 0x28003080, 0x482ad1f9, 0xf0006a40, 0xb1080002,
    0xe7bf2001, 0x6ac04826, 0x0002f020, 0x62c84924, 0xe7b72000, 0x47fce92d, 0x460f4605, 0xf0054690,
    0xf04f0607, 0xf1070a00, 0xf020000f, 0xf244070f, 0x491b30fa, 0xf7ff6208, 0x2800fe3b, 0x2001d174,
    0x62884917, 0x2f08e099, 0xf240d361, 0x491410ff, 0x2e006208, 0xeba5d047, 0x24000a06, 0xf89ae006,
    0xf80d0000, 0xf10a0004, 0x1c640a01, 0xd3f642b4, 0xe0072400, 0x0000f898, 0xf80d19a1, 0xf1080001,
    0x1c640801, 0x0008f1c6, 0xd8f342a0, 0x0a06eba5, 0x4803bf00, 0xf4006a00, 0x28003080, 0xe013d1f9,
    0x40022000, 0x45670123, 0xcdef89ab, 0x08192a3b, 0x4c5d6e7f, 0x00000004, 0x0bfa05e0, 0x00000008,
    0x0000000c, 0x00000010, 0xf8ca9800, 0x98010000, 0x0004f8ca, 0xbf00bf00, 0x0008f1c6, 0xf1c61a3f,
    0x44050008, 0xbf002600, 0x6a00486e, 0x3080f400, 0xd1f92800, 0x0000f8d8, 0xf8d86028, 0x60680004,
    0x0808f108, 0x35083f08, 0xbf00bf00, 0x2400e022, 0xf898e006, 0xf80d0000, 0xf1080004, 0x1c640801,
    0xd3f642bc, 0xe0052400, 0x20ffe02e, 0xf80d19e1, 0x1c640001, 0x0008f1c7, 0xd8f642a0, 0x4859bf00,
    0xf4006a00, 0x28003080, 0x9800d1f9, 0x98016028, 0x27006068, 0x4853bf00, 0xf4006a00, 0x28003080,
    0x4850d1f9, 0xf2446a00, 0x400831fa, 0x4608b128, 0x6208494c, 0xe8bd2001, 0x2f0087fc, 0xaf63f47f,
    0x49482000, 0xe08b6288, 0x49462001, 0xe08162c8, 0xd34b2f08, 0x10fff240, 0x62484942, 0xeba5b396,
    0x24000a06, 0xf89ae006, 0xf80d0000, 0xf10a0004, 0x1c640a01, 0xd3f642b4, 0xe0072400, 0x0000f898,
    0xf80d19a1, 0xf1080001, 0x1c640801, 0x0008f1c6, 0xd8f342a0, 0x0a06eba5, 0x4832bf00, 0xf4006a40,
    0x28003080, 0x9800d1f9, 0x0000f8ca, 0xf8ca9801, 0xbf000004, 0xf1c6bf00, 0x1a3f0008, 0x0008f1c6,
    0x26004405, 0x4827bf00, 0xf4006a40, 0x28003080, 0xf8d8d1f9, 0x60280000, 0x0004f8d8, 0xf1086068,
    0x3f080808, 0xbf003508, 0xe021bf00, 0xe0062400, 0x0000f898, 0x0004f80d, 0x0801f108, 0x42bc1c64,
    0x2400d3f6, 0x20ffe004, 0xf80d19e1, 0x1c640001, 0x0008f1c7, 0xd8f642a0, 0x4812bf00, 0xf4006a40,
    0x28003080, 0x9800d1f9, 0x98016028, 0x27006068, 0x480cbf00, 0xf4006a40, 0x28003080, 0x4809d1f9,
    0xf2446a40, 0x400831fa, 0x4608b120, 0x62484905, 0xe7702001, 0xf47f2f00, 0x2000af7b, 0x62c84901,
    0xe7682000, 0x40022000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    ],

    # Relative function addresses
    'pc_init': 0x2000004d,
    'pc_unInit': 0x20000187,
    'pc_program_page': 0x20000379,
    'pc_erase_sector': 0x20000261,
    'pc_eraseAll': 0x200001b3,

    'static_base' : 0x20000000 + 0x00000004 + 0x00000608,
    'begin_stack' : 0x20000820,
    'begin_data' : 0x20000000 + 0x1000,
    'page_size' : 0x400,
    'analyzer_supported' : False,
    'analyzer_address' : 0x00000000,
    'page_buffers' : [0x20001000, 0x20001400],   # Enable double buffering
    'min_program_length' : 0x400,

    # Relative region addresses and sizes
    'ro_start': 0x0,
    'ro_size': 0x608,
    'rw_start': 0x608,
    'rw_size': 0x14,
    'zi_start': 0x61c,
    'zi_size': 0x0,

    # Flash information
    'flash_start': 0xc000000,
    'flash_size': 0x200000,
    'sector_sizes': (
        (0x0, 0x2000),
    )
}

class STM32U585xx(CoreSightTarget):

    VENDOR = "STMicroelectronics"

    MEMORY_MAP = MemoryMap(
        FlashRegion( name="FLASH_NS", start=0x08000000,
                                    length=0x200000,
                                    access="rx",
                                    blocksize=0x2000, # Erase block size
                                    page_size=0x400,  # program page size
                                    is_boot_memory=True,
                                    are_erased_sectors_readable=False,
                                    algo=FLASH_ALGO_2M_NTZ,
                                    ),

        FlashRegion( name="FLASH_S", start=0x0C000000,
                                    length=0x200000,
                                    access="rxs",
                                    sector_size=0x2000, # Erase block size
                                    page_size=0x400,    # program page size
                                    is_boot_memory=True,
                                    are_erased_sectors_readable=False,
                                    algo=FLASH_ALGO_2M_TZEN,
                                    alias='flash_ns'
                                    ),
        RamRegion( name="SRAM_NS", start=0x0A000000, length=0xC0000, access="rwx" ),
        RamRegion( name="SRAM_S",  start=0x0E000000, length=0xC0000, access="rwxs", alias='sram_ns' ),

        RamRegion( name="SRAM4_NS", start=0x28000000, length=0x4000, access="rwx" ),
        RamRegion( name="SRAM4_S",  start=0x38000000, length=0x4000, access="rwxs", alias='sram4_ns' ),

        RomRegion( name='SYS', start=0x0FF80000, length=0x8000, access='rx' ),
        RomRegion( name='RSS', start=0x0BF90000, length=0x8000, access='srx' ),
        )

    _dev = None

    @staticmethod
    def wait_for_reg_value(reg, value, timeout=10.0):
        t_end = timeout + float(time.monotonic())
        while ( float(time.monotonic()) < t_end ):
            if int(reg) == value:
                return True
            time.sleep(0.1)
        raise Exception("Timed out while polling register")

    def __init__(self, session):
        super(STM32U585xx, self).__init__(session, self.MEMORY_MAP)
        tools_path = os.path.join(os.path.abspath(os.path.dirname(__file__)),'../')
        svd_path = os.path.join( tools_path, "svd", "STM32U5xx.svd" )
        self._svd_location = SVDFile(filename=svd_path)

    def assert_reset_for_connect(self):
        self.dp.assert_reset(1)

    def create_init_sequence(self):
        seq = super(STM32U585xx, self).create_init_sequence()
        # if self.session.options.get('connect_mode') in ('halt', 'under-reset'):
            # seq.insert_before('dp_init', ('assert_reset_for_connect', self.assert_reset_for_connect))

        if self.session.options.get('auto_unlock'):
            seq.insert_after('post_connect_hook', ('flash_auto_unlock', self.flash_auto_unlock))
        return seq

    def flash_auto_unlock(self):
        dev = self._dev

        if self.is_rw_protected():
            LOG.warning("Flash protection is enabled. Attempting mass erase operation to unlock.")
            if int(dev.FLASH.FLASH_NSCR.OPTLOCK) == 1:
                self.option_byte_unlock()

            self.disable_rw_protection()
            self.option_byte_start()
            self.mass_erase()
            self.option_byte_reload()

    def post_connect_hook(self):
        self.reset_and_halt()
        if not self._dev:
            self._dev = regmap.Device(self, self.svd_device)

    def option_byte_unlock(self):
        dev = self._dev
        if int(dev.FLASH.FLASH_NSCR.LOCK) == 1:
            # Unlock flash
            dev.FLASH.FLASH_NSKEYR = 0x45670123
            dev.FLASH.FLASH_NSKEYR = 0xCDEF89AB

        if int(dev.FLASH.FLASH_NSCR.OPTLOCK) == 1:
            # Unlock option bytes
            dev.FLASH.FLASH_OPTKEYR = 0x08192A3B
            dev.FLASH.FLASH_OPTKEYR = 0x4C5D6E7F

    def option_byte_lock(self):
        dev = self._dev
        dev.FLASH.FLASH_NSCR.OPTLOCK = 1
        dev.FLASH.FLASH_NSCR.LOCK = 1

    def option_byte_start(self):
        self._dev.FLASH.FLASH_NSCR.OPTSTRT = 1
        time.sleep(1.0)
        self.wait_for_reg_value(self._dev.FLASH.FLASH_NSSR.BSY, 0, 30)

    def option_byte_reload(self):
        try:
            self._dev.FLASH.FLASH_NSCR.OBL_LAUNCH = 1
        except pyocd.core.exceptions.TransferError:
            pass
        time.sleep(0.1)
        self.session.probe.disconnect()
        time.sleep(0.1)
        self.session.probe.connect()
        self.wait_for_reg_value(self._dev.FLASH.FLASH_NSSR.BSY, 0, 30)
        self.reset()
        self.halt()

    def get_rdp_state(self):
        # Read option bytes to determine readout protection state
        return int(self._dev.FLASH.FLASH_OPTR.RDP)

    def get_trustzone_state(self):
        return bool(int(self._dev.FLASH.FLASH_OPTR.TZEN))

    def get_boot_addr(self):
        addr = 0
        #TODO add NSBOOTADD1 case
        if self.get_security_state() == self.SecurityState.SECURE:
            addr = int(self._dev.FLASH.FLASH_SECBOOTADD0R.SECBOOTADD0) << 7
        else:
            addr = int(self._dev.FLASH.FLASH_NSBOOTADD0R.NSBOOTADD0) << 7

        return addr

    def disable_trustzone(self):
        dev = self._dev

        if int(dev.FLASH.FLASH_OPTR.TZEN) == 1:
            # If already in RDP Level 0 (0xAA), we must transition to rdp level 1 (0xDC for example) to clear the TZEN bit
            if int(dev.FLASH.FLASH_OPTR.RDP) == 0xAA:
                # Make sure the device is not OEM Locked (OEM unlock is needed first)
                assert(not self.is_oem_locked())

                if int(dev.FLASH.FLASH_NSCR.OPTLOCK) == 1:
                    self.option_byte_unlock()

                dev.FLASH.FLASH_OPTR.RDP = 0xDC

                # Force boot to the RSS region to prevent cpu lockups from an invalid image.
                dev.FLASH.FLASH_OPTR.nSWBOOT0 = 0
                dev.FLASH.FLASH_OPTR.nBOOT0 = 0

                self.option_byte_start()
                self.option_byte_reload()

                self.reset()
                self.halt()

            if int(dev.FLASH.FLASH_NSCR.OPTLOCK) == 1:
                self.option_byte_unlock()

            dev.FLASH.FLASH_OPTR.RDP = 0xAA
            dev.FLASH.FLASH_OPTR.TZEN = 0
            dev.FLASH.FLASH_OPTR.nSWBOOT0 = 1
            dev.FLASH.FLASH_OPTR.nBOOT0 = 1

            # Remove RW protection.
            if self.is_rw_protected():
                self.disable_rw_protection()

            self.option_byte_start()
            self.mass_erase()
            self.option_byte_reload()
            self.reset_and_halt()
            LOG.info("Trustzone disabled successfully.")
        else:
            LOG.info("Trustzone is already disabled.")

    def enable_trustzone(self):
        dev = self._dev
        if int(dev.FLASH.FLASH_OPTR.TZEN) == 0:

            if int(dev.FLASH.FLASH_NSCR.OPTLOCK) == 1:
                self.option_byte_unlock()

            dev.FLASH.FLASH_OPTR.TZEN = 1
            self.option_byte_start()
            self.option_byte_reload()
            self.reset_and_halt()
            LOG.info("Trustzone enabled successfully.")
        else:
            LOG.info("Trustzone is already enabled.")

    def disable_rw_protection(self):
        dev = self._dev

        # Must be in or entering 0xAA RDP state (level 0) to disable
        assert(int(dev.FLASH.FLASH_OPTR.RDP) == 0xAA)
        assert(int(dev.FLASH.FLASH_NSCR.OPTLOCK) == 0)

        # Disable SWCWM1: secure hide protection on bank 1
        dev.FLASH.FLASH_SECWM1R1.SECWM1_PSTRT = 127
        dev.FLASH.FLASH_SECWM1R1.SECWM1_PEND = 0

        # Disable WRP1A/B: write protection bank 1 areas A and B
        dev.FLASH.FLASH_WRP1AR.WRP1A_PSTRT = 127
        dev.FLASH.FLASH_WRP1AR.WRP1A_PEND = 0
        dev.FLASH.FLASH_WRP1BR.WRP1B_PSTRT = 127
        dev.FLASH.FLASH_WRP1BR.WRP1B_PEND = 0

        dev.FLASH.FLASH_SECWM1R2.HDP1_PEND = 0
        dev.FLASH.FLASH_SECWM1R2.HDP1EN = 0

        # Disable SWCWM2: secure hide protection on bank 2
        dev.FLASH.FLASH_SECWM2R1.SECWM2_PSTRT = 127
        dev.FLASH.FLASH_SECWM2R1.SECWM2_PEND = 0

        # Disable WRP2A/B: write protection bank 2 areas A and B
        dev.FLASH.FLASH_WRP2AR.WRP2A_PSTRT = 127
        dev.FLASH.FLASH_WRP2AR.WRP2A_PEND = 0
        dev.FLASH.FLASH_WRP2BR.WRP2B_PSTRT = 127
        dev.FLASH.FLASH_WRP2BR.WRP2B_PEND = 0

        dev.FLASH.FLASH_SECWM2R2.HDP2_PEND = 0
        dev.FLASH.FLASH_SECWM2R2.HDP2EN = 0

    def mass_erase(self):
        dev = self._dev

        # Mass erase page 1
        dev.FLASH.FLASH_NSCR.MER1 = 1

        # Mass erase page 2
        dev.FLASH.FLASH_NSCR.MER2 = 1

        # Start erase operation.
        dev.FLASH.FLASH_NSCR.STRT = 1

        self.wait_for_reg_value(dev.FLASH.FLASH_NSSR.BSY, 0, 30)

        assert(int(dev.FLASH.FLASH_NSSR.OPTWERR) == 0)

        # Clear MER1/MER2 bits since they persist after the operation.
        dev.FLASH.FLASH_NSCR.MER1 = 0
        dev.FLASH.FLASH_NSCR.MER2 = 0

        return True

    def is_rw_protected(self):
        rslt = False
        dev = self._dev

        rslt |= int(dev.FLASH.FLASH_SECWM1R2.HDP1EN)
        rslt |= int(dev.FLASH.FLASH_SECWM2R2.HDP2EN)

        # WRPxxR.UNLOCK is 1 if region is not locked.
        rslt |= ( int(dev.FLASH.FLASH_WRP1AR.UNLOCK) == 0 )
        rslt |= ( int(dev.FLASH.FLASH_WRP1BR.UNLOCK) == 0 )
        rslt |= ( int(dev.FLASH.FLASH_WRP1AR.UNLOCK) == 0 )
        rslt |= ( int(dev.FLASH.FLASH_WRP1BR.UNLOCK) == 0 )

        if rslt > 0:
            return True
        else:
            return False

    def is_locked(self):
        return self.is_rw_protected()

    def is_oem_locked(self):
        rslt = False
        dev = self._dev

        rslt |= int(dev.FLASH.FLASH_NSSR.OEM1LOCK)
        rslt |= int(dev.FLASH.FLASH_NSSR.OEM2LOCK)
        return rslt
