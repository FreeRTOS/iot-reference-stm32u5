#!python
#
#  FreeRTOS STM32 Reference Integration
#
#  Copyright (C) 2022 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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

import argparse
import os

import jinja2
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.serialization import (
    load_pem_private_key,
    load_pem_public_key,
)
from imgtool import keys as imgtool_keys


def get_pub_bytes_str(key):
    keyByteArray = key.get_public_bytes()
    key_list = []
    for idx in range(0, len(keyByteArray), 8):
        last_idx = idx + 8

        if len(keyByteArray) < last_idx:
            last_idx = len(keyByteArray) - idx

        num_bytes = last_idx - idx
        fmt_str = ("0x{:02X}, " * (num_bytes)).strip()
        lineBytes = keyByteArray[idx:last_idx]

        line = fmt_str.format(*lineBytes)
        key_list.append(line)
    return key_list, len(keyByteArray)


def _main():
    script_dir = os.path.dirname(os.path.realpath(__file__))
    mcuboot_dir = os.path.realpath(
        os.path.join(
            script_dir,
            "..",
            "Middleware",
            "ARM",
            "trusted-firmware-m",
            "bl2",
            "ext",
            "mcuboot",
        )
    )

    parser = argparse.ArgumentParser(
        description="Generate an mcuboot keys.c file containing the public portion of the specified keys."
    )

    parser.add_argument(
        "--spe-key",
        help="Path to spe public or private key.",
        type=str,
        default="spe_signing_key.pem",
    )
    parser.add_argument(
        "--nspe-key",
        help="Path to nspe public or private key.",
        type=str,
        default="nspe_signing_key.pem",
    )
    parser.add_argument(
        "-o",
        "--output",
        help="Path to output keys.c file.",
        type=str,
        default=os.path.join(mcuboot_dir, "keys.c"),
    )

    args = parser.parse_args()

    spe_key = imgtool_keys.load(args.spe_key)
    nspe_key = imgtool_keys.load(args.nspe_key)

    spe_key_pub, spe_key_pub_len = get_pub_bytes_str(spe_key)
    nspe_key_pub, nspe_key_pub_len = get_pub_bytes_str(nspe_key)

    template_path = os.path.join(os.path.dirname(__file__), "keys.c.jinja2")

    template = None
    with open(template_path) as template_file:
        template = jinja2.Template(template_file.read())

    keys_output = template.render(
        spe_key_pub=spe_key_pub,
        spe_key_pub_len=spe_key_pub_len,
        nspe_key_pub=nspe_key_pub,
        nspe_key_pub_len=nspe_key_pub_len,
    )

    with open(args.output, "w") as outfile:
        outfile.write(keys_output)


if __name__ == "__main__":
    _main()
