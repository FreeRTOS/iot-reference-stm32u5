/*
 * FreeRTOS STM32 Reference Integration
 *
 * Copyright (c) 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _CLI_PKI_PRV_H
#define _CLI_PKI_PRV_H

BaseType_t xPkcs11GenerateKeyPairEC( char * pcPrivateKeyLabel,
                                     char * pcPublicKeyLabel,
                                     unsigned char ** ppucPublicKeyDer,
                                     size_t * puxPublicKeyDerLen );

BaseType_t xPkcs11InitMbedtlsPkContext( char * pcLabel,
                                        mbedtls_pk_context * pxPkCtx,
                                        CK_SESSION_HANDLE_PTR pxSessionHandle );

BaseType_t xPkcs11ReadCertificate( mbedtls_x509_crt * pxCertificateContext,
                                   const char * pcCertLabel );

BaseType_t xPkcs11WriteCertificate( const char * pcLabel,
                                    mbedtls_x509_crt * pxCertificateContext );


BaseType_t xPkcs11ExportPublicKey( char * pcPubKeyLabel,
                                   unsigned char ** ppucPublicKeyDer,
                                   size_t * puxPubKeyDerLen );

#endif /* _CLI_PKI_PRV_H */
