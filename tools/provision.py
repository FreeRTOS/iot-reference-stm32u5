#!python
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
import argparse
import json
import logging
import os
import random
import string
from time import time
from TargetDevice import TargetDevice
from CryptoUtils import CryptoUtils
from AWSHelper import AWSHelper

import boto3
import requests
import serial
import serial.tools.list_ports

logger = logging.getLogger()

def find_serial_port(usbVendorId=0x0483, usbProductId=0x374E):
    ports = serial.tools.list_ports.comports()
    matches = []
    device = None

    for port in ports:
        attrs = dir(port)
        if (
            "vid" in attrs
            and port.vid
            and "pid" in attrs
            and port.pid
            and "device" in attrs
            and port.device
        ):
            logging.debug(
                "Found device: {} with Manufacturer: {}, Vendor ID: {}, and Product ID: {}".format(
                    port.device, port.manufacturer, hex(port.vid), hex(port.pid)
                )
            )
            if port.vid == usbVendorId and port.pid == usbProductId:
                matches.append(port.device)
    # default to an empty string if no match was found.
    if len(matches) > 0:
        device = matches[0]
    return device


def get_unix_timestamp():
    return int(time())


def interactive_config(target):
    print(
        "Interactive configuration mode: Press return to use defaults (displayed in brackets)"
    )
    cfg = target.conf_get_all()
    for key, value in cfg.items():
        new_value = str(input("{}[{}]: ".format(key, value)))
        if len(new_value) > 0:
            target.conf_set(key, new_value)


def provision_pki(target, aws, cert_issuer):
    thing_name = target.conf_get("thing_name")

    # Generate a key
    print("Generating a new public/private key pair")
    pub_key = target.generate_key()

    if not validate_pubkey(pub_key):
        print("Error: Could not parse public key.")
        raise SystemExit

    if cert_issuer == "aws":
        provision_pki_csr( target, aws )
    elif cert_issuer == "self":
        provision_pki_self( target, aws )
    else:
        print("Error: Unknown certificate issuer.")
        raise SystemExit

    ca_cert = get_ca_by_label("SFSRootCAG2")
    ca_cert_pem = ca_cert.as_pem()

    if not ca_cert_pem:
        print("Error: Failed to load CA Certificate.")
        raise SystemExit
    else:
        print('Importing Root Ca Certificate: "{}"'.format(ca_cert.common_name))
        target.write_cert(ca_cert_pem, label="root_ca_cert")

def provision_pki_self(target, aws):
    print("Generating a self-signed Certificate")

    # Generate a cert (returned in byte-string form)
    cert = target.generate_cert()

    if not validate_certificate(cert, pub_key, thing_name):
        print("Error: Certificate is invalid.")
        raise SystemExit

    # aws api requires csr in utf-8 string form.
    thing_data = aws.register_thing_cert(thing_name, cert.decode("utf-8"))


def provision_pki_csr(target, aws):
    print("Generating a Certificate Signing Request")

    # Generate a csr (returned in byte-string form)
    csr = target.generate_csr()

    if not validate_csr(csr, pub_key, thing_name):
        print("Error: CSR is invalid.")
        raise SystemExit

    # aws api requires csr in utf-8 string form.
    thing_data = aws.register_thing_csr(thing_name, csr.decode("utf-8"))

    if "certificatePem" in thing_data:
        target.write_cert(thing_data["certificatePem"])
    else:
        print("Error: No certificate returned from register_thing_csr call.")
        raise SystemExit


def process_args():
    parser = argparse.ArgumentParser(argument_default=argparse.SUPPRESS)

    # "Interactive mode" for asking about config details.
    parser.add_argument("-i", "--interactive", action="store_true")
    parser.add_argument("-v", "--verbose", action="store_true")

    # Default to stlink vid/pid if only one is connected, otherwise error.
    parser.add_argument("-d", "--device", type=str)

    # Wifi config
    parser.add_argument("--wifi-ssid", type=str)
    parser.add_argument("--wifi-credential", type=str)

    # User specified device name (random otherwise)
    parser.add_argument("--thing-name", type=str)

    # User specified certificate issuer (self-signed or aws issued via CSR)
    parser.add_argument(
        "--cert-issuer", default="self", choices=("self", "aws"), type=str
    )

    # Use defaults from aws config, but allow user to override
    parser.add_argument("--aws-profile", type=str, default="default")
    parser.add_argument("--aws-region", type=str)
    parser.add_argument("--aws-access-key-id", type=str)
    parser.add_argument("--aws-access-key-secret", type=str)

    return parser.parse_args()


def configure_target(args, target):
    # Override current config with cli provided config
    if "wifi_ssid" in args:
        target.conf_set("wifi_ssid", args.wifi_ssid)

    if "wifi_credential" in args:
        target.conf_set("wifi_credential", args.wifi_credential)

    if "thing_name" in args:
        target.conf_set("thing_name", args.thing_name)

    # Generate a random thing-name if necessary
    thing_name = target.conf_get("thing_name")
    if not thing_name or thing_name == "":
        lexicon = string.ascii_letters + string.digits
        thing_name = "".join(random.choice(lexicon) for i in range(16))
        target.conf_set("thing_name", thing_name)

    # Set time high water mark
    timestamp = get_unix_timestamp()
    target.conf_set("time_hwm", str(timestamp))


def main():
    args = process_args()

    if "verbose" in args:
        logging.getLogger().setLevel(logging.DEBUG)

    devpath = find_serial_port()
    if "device" in args:
        devpath = args.device

    if not devpath or len(devpath) == 0:
        logging.error(
            'Target device path could not be determined automatically. Please call this script with the "device" argument'
        )
        raise SystemExit
    else:
        print("Target device path: {}".format(devpath))

    print("Connecting to target...")

    target = TargetDevice(devpath, 115200)

    configure_target(args, target)

    # Initialize a connection to AWS IoT
    aws = AwsHelper(args=args)
    if not aws.check_credentials():
        print("The provided AWS account credentials are inalid.")
        raise SystemExit

    target.conf_set("mqtt_endpoint", aws.get_endpoint())

    # Allow user to modify configuration if interactive mode was selected.
    if "interactive" in args:
        interactive_config(target)

    print("Commiting target configuration...")
    target.conf_commit()

    provision_pki(target, aws, args.cert_issuer)

    print("Provisioning process complete. Resetting target device...")
    target.reset()


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="[ %(levelname)s ] %(message)s (%(filename)s:%(funcName)s)",
    )
    main()
