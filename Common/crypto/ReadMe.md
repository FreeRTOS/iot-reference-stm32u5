### PkiObject API
The PkiObject API takes care of some of the mundane tasks in converting between different representations of cryptographic objects such as public keys, private keys, and certificates.

Files located in this folder belong to the PkiObject module.

This API can be accessed via the `pki` CLI command which is implemented in the `Common/cli/cli_pki.c` file.
```
pki:
    Perform public/private key operations.
    Usage:
    pki <verb> <object> <args>
        Valid verbs are { generate, import, export, list }
        Valid object types are { key, csr, cert }
        Arguments should be specified in --<arg_name> <value>

    pki generate key <label_public> <label_private> <algorithm> <algorithm_param>
        Generates a new private key to be stored in the specified labels

    pki generate csr <label>
        Generates a new Certificate Signing Request using the private key
        with the specified label.
        If no label is specified, the default tls private key is used.

    pki generate cert <cert_label> <private_key_label>
        Generate a new self-signed certificate

    pki import cert <label>
        Import a certificate into the given slot. The certificate should be
        copied into the terminal in PEM format, ending with two blank lines.

    pki export cert <label>
        Export the certificate with the given label in pem format.
        When no label is specified, the default certificate is exported.

    pki import key <label>
        Import a public key into the given slot. The key should be
        copied into the terminal in PEM format, ending with two blank lines.

    pki export key <label>
        Export the public portion of the key with the specified label.
```
