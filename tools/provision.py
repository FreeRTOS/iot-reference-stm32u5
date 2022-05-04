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
import io
import json
import logging
import os
import random
import string
from time import monotonic

import boto3
import requests
import serial
import serial.tools.list_ports
from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.hazmat.primitives.serialization import load_pem_public_key
from cryptography.x509.oid import NameOID

logger = logging.getLogger()


class TargetDevice:
    _running_config = None
    _staged_config = None
    _error_bstr = (b"error", b"Error", b"ERROR", b"<ERR>")
    _timeout = 2.0

    class ReadbackError(Exception):
        """Raised when the command readback received from the target does not match the command sent."""

        pass

    class ResponseTimeout(Exception):
        """Raised when the target fails to respond within the alloted timeout"""

        pass

    class TargetError(Exception):
        """Raised when the target returns an error message"""

        pass

    def __init__(self, device, baud):
        """Connect to a target device"""
        self.ser = serial.Serial(device, baud, timeout=0.1, rtscts=False)
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()
        self.sio = io.BufferedRWPair(self.ser, self.ser)
        self.sio._CHUNK_SIZE = 2
        self._sync()
        self._config_read_from_target()
        self._staged_config = {}

    def _sync(self):
        """Send Control+C (0x03) conntrol code to clear the current line."""
        self.sio.write(b"\x03")
        self.sio.flush()
        self._read_response()

    def _send_cmd(self, *args, timeout=_timeout):
        """Send a single command to the target and return True if the readback matches."""
        cmd = b" ".join(args)
        cmdstr = cmd + b"\r\n"

        self.sio.write(cmdstr)
        logging.debug("TX: {} (_send_cmd)".format(cmdstr))

        self.sio.flush()

        timeoutTime = monotonic() + timeout

        cmd_readback = self.sio.readline()
        while timeoutTime > monotonic():
            if len(cmd_readback) > 0:
                break
            cmd_readback = self.sio.readline()

        logging.debug("RX: {} (_send_cmd)".format(cmd_readback))
        if cmd not in cmd_readback:
            raise TargetDevice.ReadbackError()

    def _read_response(self, timeout=_timeout):
        """Read the response to a command"""
        response = []
        errorFlag = False
        timeoutFlag = True

        timeoutTime = monotonic() + timeout

        while timeoutTime > monotonic():
            line = self.sio.readline()

            if len(line) == 0:
                continue

            logging.debug("RX: {} (_read_response)".format(line))

            if b"> " == line:
                timeoutFlag = False
                break
            elif any(errStr in line for errStr in self._error_bstr):
                errorFlag = True

            response.append(line)

        if errorFlag:
            raise TargetDevice.TargetError()
        elif timeoutFlag:
            raise TargetDevice.ResponseTimeout()

        return response

    def _read_pem(self, timeout=_timeout):
        """Read a pem file from a command response and return it as a byte string"""
        beginStr = b"-----BEGIN "
        endStr = b"-----END "

        pem = []

        readingPemFlag = False
        errorFlag = False
        timeoutFlag = True

        timeoutTime = monotonic() + timeout

        while monotonic() < timeoutTime:
            line = self.sio.readline()

            if len(line) == 0:
                continue

            logging.debug("RX: {} (_read_pem)".format(line))

            # normalize line endings
            line = line.replace(b"\r\n", b"\n")

            if b"> " in line:
                if line == b"> ":
                    errorFlag = True
                    timeoutFlag = False
                    break
                else:
                    line = line.replace(b"> ", b"")

            if readingPemFlag:
                pem.append(line)
                if endStr in line:
                    timeoutFlag = False
                    break
            elif beginStr in line:
                readingPemFlag = True
                pem.append(line)
            elif any(errStr in line for errStr in self._error_bstr):
                errorFlag = True
                break

        if errorFlag:
            raise TargetDevice.TargetError()
        elif timeoutFlag:
            raise TargetDevice.ResponseTimeout()
        # Handle any lines in output after the pem footer
        else:
            self._read_response()

        combined = b"".join(pem)
        return combined

    def _write_pem(self, pem):
        """Write a pem bytestring to the target and raise an error if the readback does not match."""
        # Remove any existing CR
        pem = pem.replace(b"\r\n", b"\n")
        for line in pem.split(b"\n"):
            line = line + b"\r\n"
            logging.debug("TX: {} (_write_pem)".format(line))
            self.sio.write(line)

        self.sio.write(b"\r\n\r\n")
        self.sio.flush()

        pemReadback = self._read_pem(timeout=5)

        if pem != pemReadback:
            raise TargetDevice.ReadbackError(
                "Readback does not match provided pem file."
            )

    def reset(self):
        self._send_cmd(b"reset")

    def write_cert(self, cert, label=None):
        """Write a certificate in pem format to the specified label."""
        if label:
            self._send_cmd(b"pki import cert", bytes(label, "ascii"))
        else:
            self._send_cmd(b"pki import cert")

        self._write_pem(cert)

    def generate_key(self, label=None):
        """Returns a byte string containing the public key of a newly generated keypair on the target"""
        if label:
            self._send_cmd(b"pki generate key", bytes(label, "ascii"))
        else:
            self._send_cmd(b"pki generate key")

        pubkey = self._read_pem()

        return pubkey

    def generate_csr(self):
        """Return a byte string containing a newly generated certificate signing request from the target."""
        self._send_cmd(b"pki generate csr")
        csr = self._read_pem()
        return csr

    def generate_cert(self):
        self._send_cmd(b"pki generate cert")
        cert = self._read_pem()
        return cert

    def _config_read_from_target(self):
        conf = {}

        self._send_cmd(b"conf get")
        resp = self._read_response()

        for line in resp:
            # Remove newlines and quotes
            line = line.replace(b"\r\n", b"")
            line = line.replace(b'"', b"")
            if b"=" in line:
                key, value = line.split(b"=")
                conf[key] = value

        if len(conf) > 0:
            self._running_config = conf

    def conf_commit(self):
        assert self._running_config
        assert self._staged_config

        for key in self._staged_config:
            # running config and staged config mismatched
            if (
                key in self._running_config
                and self._staged_config[key] != self._running_config[key]
            ):
                self._send_cmd(b"conf set", key, self._staged_config[key])
                self._read_response()
            # Handle new keys
            elif key not in self._running_config:
                self._send_cmd(b"conf set", key, self._staged_config[key])
                self._read_response()

        if len(self._staged_config) > 0:
            self._send_cmd(b"conf commit")
            self._read_response()

        # Otherwise, no changes to write

    """Get the specified key as a string"""

    def conf_get(self, key):
        # Convert fromt ascii to byte string
        key = bytes(key, "ascii")

        if key in self._staged_config:
            return self._staged_config[key].decode("UTF-8")
        elif key in self._running_config:
            return self._running_config[key].decode("UTF-8")
        else:
            return None

    def conf_get_all(self):
        assert self._running_config
        assert self._staged_config

        cfg = {}

        keys = set(self._running_config.keys())
        keys.update(self._staged_config.keys())

        for key in keys:
            if key in self._staged_config:
                cfg[key.decode("UTF-8")] = self._staged_config[key].decode("UTF-8")
            elif key in self._running_config:
                cfg[key.decode("UTF-8")] = self._running_config[key].decode("UTF-8")
        return cfg

    def conf_set(self, key, value):
        assert self._running_config is not None
        assert self._staged_config is not None

        key_b = bytes(key, "ascii")
        value_b = bytes(value, "ascii")
        if self._running_config.get(key_b, None) != value_b:
            self._staged_config[key_b] = value_b


class AwsHelper:
    session = None
    session_valid = False
    iot_client = None
    thing = None
    userId = None
    account = None
    arn = None

    def __init__(self, args):
        profile = "default"
        if "aws-profile" in args:
            profile = args.aws_profile

        region = None
        if "aws-region" in args:
            region = args.aws_region

        access_key_id = None
        if "aws-access-key-id" in args:
            access_key_id = args.aws_access_key_id

        secret_access_key = None
        if "aws-access-key-secret" in args:
            secret_access_key = args.aws_access_key_secret

        self.session = boto3.session.Session(
            profile_name=profile,
            region_name=region,
            aws_access_key_id=access_key_id,
            aws_secret_access_key=secret_access_key,
        )
        self.check_credentials()

    def check_credentials(self):
        if not self.session_valid and self.session:
            sts = self.session.client("sts")
            caller_id = sts.get_caller_identity()
            if caller_id:
                self.session_valid = True
                self.userId = caller_id["UserId"]
                self.account = caller_id["Account"]
                self.arn = caller_id["Arn"]
        return self.session_valid

    def get_session(self):
        if self.check_credentials():
            return self.session
        else:
            return None

    def get_client(self, client_type):
        client = None
        if self.get_session():
            client = self.session.client(client_type)
        return client

    def get_endpoint(self):
        endpoint_address = ""
        if not self.iot_client:
            self.iot_client = self.get_client("iot")

        if self.iot_client:
            response = self.iot_client.describe_endpoint(endpointType="iot:Data-ATS")

            if "endpointAddress" in response:
                endpoint_address = response["endpointAddress"]

        return endpoint_address

    def create_policy(self):
        if not self.iot_client:
            self.iot_client = self.get_client("iot")

        policies = self.iot_client.list_policies()

        policyFound = False
        for policy in policies["policies"]:
            logger.debug("Found Policy: {}".format(policy["policyName"]))
            if policy["policyName"] == "AllowAllDev":
                policyFound = True

        if policyFound:
            logger.debug('Found existing "AllowAllDev" IoT core policy.')
        else:
            logger.info('Existing policy "AllowAllDev" was not found. Creating it...')

        policyDocument = {
            "Version": "2012-10-17",
            "Statement": [{"Effect": "Allow", "Action": "iot:*", "Resource": "*"}],
        }

        if not policyFound:
            policy = self.iot_client.create_policy(
                policyName="AllowAllDev",
                policyDocument=json.dumps(policyDocument),
            )

    # Register a device with IoT core and return the certificate
    def register_thing_csr(self, thing_name, csr):
        if not self.iot_client:
            self.iot_client = self.get_client("iot")

        assert self.iot_client

        self.thing = {}

        cli = self.iot_client

        cert_response = cli.create_certificate_from_csr(
            certificateSigningRequest=csr, setAsActive=True
        )
        logging.debug("CreateCertificateFromCsr response: {}".format(cert_response))
        self.thing.update(cert_response)

        create_thing_resp = cli.create_thing(thingName=thing_name)
        logging.debug("CreateThing response: {}".format(create_thing_resp))
        self.thing.update(create_thing_resp)

        if not (
            "certificateArn" in self.thing
            and "thingName" in self.thing
            and "certificatePem" in self.thing
        ):
            logging.error("Error: Certificate creation failed.")
        else:
            print(
                "Attaching thing: {} to principal: {}".format(
                    self.thing["thingName"], self.thing["certificateArn"]
                )
            )
            cli.attach_thing_principal(
                thingName=self.thing["thingName"],
                principal=self.thing["certificateArn"],
            )

        # Check for / create Policy
        self.create_policy()

        # Attach the policy to the principal.
        print('Attaching the "AllowAllDev" policy to the device certificate.')
        self.iot_client.attach_policy(
            policyName="AllowAllDev", target=self.thing["certificateArn"]
        )

        self.thing["certificatePem"] = bytes(
            self.thing["certificatePem"].replace("\\n", "\n"), "ascii"
        )

        return self.thing.copy()

    # Register a device with IoT core with a given certificate
    def register_thing_cert(self, thing_name, cert):
        if not self.iot_client:
            self.iot_client = self.get_client("iot")

        assert self.iot_client

        self.thing = {}

        cli = self.iot_client

        cert_response = cli.register_certificate_without_ca(
            certificatePem=cert, status="ACTIVE"
        )
        logging.debug("RegisterCertificateWithoutCA response: {}".format(cert_response))
        self.thing.update(cert_response)

        create_thing_resp = cli.create_thing(thingName=thing_name)
        logging.debug("CreateThing response: {}".format(create_thing_resp))
        self.thing.update(create_thing_resp)

        if not ("certificateArn" in self.thing and "thingName" in self.thing):
            logging.error("Error: Certificate creation failed.")
        else:
            print(
                "Attaching thing: {} to principal: {}".format(
                    self.thing["thingName"], self.thing["certificateArn"]
                )
            )
            cli.attach_thing_principal(
                thingName=self.thing["thingName"],
                principal=self.thing["certificateArn"],
            )

        # Check for / create Policy
        self.create_policy()

        # Attach the policy to the principal.
        print('Attaching the "AllowAllDev" policy to the device certificate.')
        self.iot_client.attach_policy(
            policyName="AllowAllDev", target=self.thing["certificateArn"]
        )

        return self.thing.copy()


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
    import time

    return int(time.time())


def interactive_config(target):
    print(
        "Interactive configuration mode: Press return to use defaults (displayed in brackets)"
    )
    cfg = target.conf_get_all()
    for key, value in cfg.items():
        new_value = str(input("{}[{}]: ".format(key, value)))
        if len(new_value) > 0:
            target.conf_set(key, new_value)


def validate_pubkey(pub_key):
    result = True
    key = None
    try:
        key = load_pem_public_key(pub_key)
    except ValueError:
        result = False

    if result and isinstance(key, rsa.RSAPublicKey):
        if key.key_size < 2048:
            logging.error("Error: RSA key size must be >= 2048 bits")
            result = False
    elif result and isinstance(key, ec.EllipticCurvePublicKey):
        if not (
            isinstance(key.curve, ec.SECP256R1) or isinstance(key.curve, ec.SECP384R1)
        ):
            logging.error("Error: EC keys must of type secp256r1 or secp384r1.")
            result = False
    elif result:
        logging.error("Error: Public keys must be either RSA or EC type")
        result = False

    return result


def validate_certificate(cert_pem, pub_key_pem, thing_name):
    result = True
    pubkey = None
    cert = None
    try:
        pubkey = load_pem_public_key(pub_key_pem)
    except ValueError:
        result = False

    try:
        cert = x509.load_pem_x509_certificate(cert_pem)
    except ValueError:
        result = False

    if result:
        # Check that public key matches
        if not cert.public_key().public_numbers() == pubkey.public_numbers():
            logging.error(
                "Error: Certificate does not match public key: {}, {}".format(
                    cert.public_key(), pubkey
                )
            )
            result = False

    if result:
        thingNameFound = False
        # Check that subject CN is the thing name
        for attr in cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME):
            rfc4514_str = attr.rfc4514_string()
            logging.debug("Parsed Certificate Attribute: {}".format(rfc4514_str))
            if attr.value == thing_name:
                thingNameFound = True

        if not thingNameFound:
            logging.error("Error: Did not find thing name in Certificate subject.")
            result = False

    if result:
        thingNameFound = False
        # Check that issuer CN is the thing name
        for attr in cert.issuer.get_attributes_for_oid(NameOID.COMMON_NAME):
            rfc4514_str = attr.rfc4514_string()
            logging.debug("Parsed Certificate Attribute: {}".format(rfc4514_str))
            if attr.value == thing_name:
                thingNameFound = True

        if not thingNameFound:
            logging.error("Error: Did not find thing name in Certificate Issuer")
            result = False
    return result


def validate_csr(csr_pem, pub_key_pem, thing_name):
    result = True
    pubkey = None
    csr = None
    try:
        pubkey = load_pem_public_key(pub_key_pem)
    except ValueError:
        result = False

    try:
        csr = x509.load_pem_x509_csr(csr_pem)
    except ValueError:
        result = False

    if result:
        # Check that public key matches
        if not csr.public_key().public_numbers() == pubkey.public_numbers():
            logging.error(
                "Error: CSR does not match public key: {}, {}".format(
                    csr.public_key(), pubkey
                )
            )
            result = False

    if result:
        thingNameFound = False
        # Check that CN is the thing name
        for attr in csr.subject.get_attributes_for_oid(NameOID.COMMON_NAME):
            rfc4514_str = attr.rfc4514_string()
            logging.debug("Parsed CSR Attribute: {}".format(rfc4514_str))
            if attr.value == thing_name:
                thingNameFound = True

        if not thingNameFound:
            logging.error("Error: Did not find thing name in CSR")
            result = False
    return result


def validate_ca_certificate(cert):
    result = True
    try:
        x509Cert = x509.load_pem_x509_certificate(cert["pem"])
    except ValueError:
        logging.error(
            "Failed to parse certificate: CN: {}, O: {}".format(cert["CN"], cert["O"])
        )
        result = False

    if result:
        for attr in x509Cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME):
            rfc4514_str = attr.rfc4514_string()
            logging.debug("Parsed Cert Attribute: {}".format(rfc4514_str))
            if not ("CN=" in rfc4514_str and attr.value == cert["CN"]):
                result = False

        for attr in x509Cert.subject.get_attributes_for_oid(NameOID.ORGANIZATION_NAME):
            rfc4514_str = attr.rfc4514_string()
            logging.debug("Parsed Cert Attribute: {}".format(rfc4514_str))
            if not ("O=" in rfc4514_str and attr.value == cert["O"]):
                result = False

    return result


def get_amazon_rootca_certs():
    AmazonTrustRootCAs = [
        {
            "CN": "Amazon Root CA 1",
            "O": "Amazon",
            "label": "AmazonRootCA1",
            "pem": None,
        },
        {
            "CN": "Amazon Root CA 2",
            "O": "Amazon",
            "label": "AmazonRootCA2",
            "pem": None,
        },
        {
            "CN": "Amazon Root CA 3",
            "O": "Amazon",
            "label": "AmazonRootCA3",
            "pem": None,
        },
        {
            "CN": "Amazon Root CA 4",
            "O": "Amazon",
            "label": "AmazonRootCA4",
            "pem": None,
        },
        {
            "CN": "Starfield Services Root Certificate Authority - G2",
            "O": "Starfield Technologies, Inc.",
            "label": "SFSRootCAG2",
            "pem": None,
        },
    ]
    AmazonTrustRepositoryUrl = "https://www.amazontrust.com/repository/"

    if not os.path.exists(".cache"):
        os.mkdir(".cache")

    for i in range(len(AmazonTrustRootCAs)):
        cert = AmazonTrustRootCAs[i]
        cacheFile = os.path.join(".cache", cert["label"] + ".pem")
        cert["pem"] = None

        # Check for cached copy and validate it
        if os.path.exists(cacheFile):
            with open(cacheFile, "rb") as f:
                cert["pem"] = f.read()

            if not validate_ca_certificate(cert):
                os.remove(cacheFile)
                cert["pem"] = None

        if not cert["pem"]:
            r = requests.get(AmazonTrustRepositoryUrl + cert["label"] + ".pem")
            if r.ok:
                cert["pem"] = r.content
                if validate_ca_certificate(cert):
                    with open(cacheFile, "wb") as f:
                        f.write(cert["pem"])
                else:
                    cert["pem"] = None
        if cert["pem"]:
            AmazonTrustRootCAs[i] = cert

    return AmazonTrustRootCAs


def provision_pki(target, aws, cert_issuer):
    thing_name = target.conf_get("thing_name")

    # Generate a key
    print("Generating a new public/private key pair")
    pub_key = target.generate_key()

    if not validate_pubkey(pub_key):
        print("Error: Could not parse public key.")
        raise SystemExit

    if cert_issuer == "aws":
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
    elif cert_issuer == "self":
        print("Generating a self-signed Certificate")

        # Generate a cert (returned in byte-string form)
        cert = target.generate_cert()

        if not validate_certificate(cert, pub_key, thing_name):
            print("Error: Certificate is invalid.")
            raise SystemExit

        # aws api requires csr in utf-8 string form.
        thing_data = aws.register_thing_cert(thing_name, cert.decode("utf-8"))
    else:
        print("Error: Unknown certificate issuer.")
        raise SystemExit

    ca_certs = get_amazon_rootca_certs()
    if ca_certs:
        for cert in ca_certs:
            if cert["label"] == "SFSRootCAG2":
                print('Importing root ca certificate: "{}"'.format(cert["CN"]))
                target.write_cert(cert["pem"], label="root_ca_cert")


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
