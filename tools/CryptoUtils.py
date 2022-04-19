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

from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.hazmat.primitives.serialization import load_pem_public_key
from cryptography.x509.oid import NameOID
from dataclasses import dataclass

class CryptoUtils:
    @dataclass
    class CACertificate:
        """Keep track of a single CA Certificate"""
        common_name: str
        organization: str
        label: str
        pem: str

        def __init__(self, subject_common_name: str, subject_organization: str, label: str):
            self.subject_common_name = subject_common_name
            self.subject_organization = subject_organization
            self.label = label

        def as_pem(self):
            AmazonTrustRepositoryUrl = "https://www.amazontrust.com/repository/"

            pem = None

            if not os.path.exists(".cache"):
                os.mkdir(".cache")

            cacheFile = os.path.join(".cache", self.label + ".pem")
            # Check for cached copy and validate it
            if os.path.exists(cacheFile):
                with open(cacheFile, "rb") as f:
                    pem = f.read()

                if not self.__validate(pem):
                    os.remove(cacheFile)
                    pem = None

                if not pem:
                    r = requests.get(AmazonTrustRepositoryUrl + self.label + ".pem")
                    if r.ok:
                        pem = r.content
                        if self.__validate(pem):
                            with open(cacheFile, "wb") as f:
                                f.write(pem)
                        else:
                            pem = None
            return pem

        def __validate(self, ca_cert_pem: bytes):
            result = True

            try:
                x509Cert = x509.load_pem_x509_certificate(ca_cert_pem)
            except ValueError:
                logging.error(
                    "Failed to parse certificate: Common Name: {}, Organization: {}".format(self.common_name, self.organization)
                )
                result = False

            if result:
                for attr in x509Cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME):
                    rfc4514_str = attr.rfc4514_string()
                    logging.debug("Parsed Cert Attribute: {}".format(rfc4514_str))
                    if not ("CN=" in rfc4514_str and attr.value == self.common_name):
                        result = False

                for attr in x509Cert.subject.get_attributes_for_oid(NameOID.ORGANIZATION_NAME):
                    rfc4514_str = attr.rfc4514_string()
                    logging.debug("Parsed Cert Attribute: {}".format(rfc4514_str))
                    if not ("O=" in rfc4514_str and attr.value == self.organization):
                        result = False

            return result

    def get_ca_by_label(label: str):
        for cert in self.AmazonTrustRootCAs:
            if cert.label == label:
                return cert

    AmazonTrustRootCAs = (
        CACertificate(
            "Amazon Root CA 1",
            "Amazon",
            "AmazonRootCA1" ),
        CACertificate(
            "Amazon Root CA 2",
            "Amazon",
            "AmazonRootCA2" ),
        CACertificate(
            "Amazon Root CA 3",
            "Amazon",
            "AmazonRootCA3"),
        CACertificate(
            "Amazon Root CA 4",
            "Amazon",
            "AmazonRootCA4"),
        CACertificate(
            "Starfield Services Root Certificate Authority - G2",
            "Starfield Technologies, Inc.",
            "SFSRootCAG2",
        )
    )

    def validate_csr(csr_pem: bytes, pub_key_pem: bytes, common_name: str):
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
            commonNameFound = False
            # Check that CN is the thing name
            for attr in csr.subject.get_attributes_for_oid(NameOID.COMMON_NAME):
                rfc4514_str = attr.rfc4514_string()
                logging.debug("Parsed CSR Attribute: {}".format(rfc4514_str))
                if attr.value == common_name:
                    commonNameFound = True

            if not commonNameFound:
                logging.error("Error: Did not find common name in CSR")
                result = False
        return result

    def validate_pubkey(pub_key_pem: bytes):
        result = True
        key = None
        try:
            key = load_pem_public_key(pub_key_pem)
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

    def validate_certificate_from_csr(cert_pem: bytes, pub_key_pem: bytes, common_name: str):
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
            commonNameFound = False
            # Check that subject CN is the thing name
            for attr in cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME):
                rfc4514_str = attr.rfc4514_string()
                logging.debug("Parsed Certificate Attribute: {}".format(rfc4514_str))
                if attr.value == common_name:
                    commonNameFound = True

            if not commonNameFound:
                logging.error("Error: Did not find \"{}\" in Certificate subject.".format(common_name))
                result = False
        return result

    def validate_certificate_self_signed(cert_pem, pub_key_pem, common_name):
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
            commonNameFound = False
            for attr in cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME):
                rfc4514_str = attr.rfc4514_string()
                logging.debug("Parsed Certificate Attribute: {}".format(rfc4514_str))
                if attr.value == common_name:
                    commonNameFound = True

            if not commonNameFound:
                logging.error("Error: Did not find \"{}\" in Certificate Subject.".format(common_name))
                result = False

        if result:
            commonNameFound = False
            for attr in cert.issuer.get_attributes_for_oid(NameOID.COMMON_NAME):
                rfc4514_str = attr.rfc4514_string()
                logging.debug("Parsed Certificate Attribute: {}".format(rfc4514_str))
                if attr.value == common_name:
                    commonNameFound = True

            if not commonNameFound:
                logging.error("Error: Did not find \"{}\" in Certificate Issuer.".format(common_name))
                result = False
        return result
