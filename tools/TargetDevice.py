import serial
import io
from time import monotonic,time
import logging


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


    def conf_get(self, key):
        """Get the specified key as a string"""
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

