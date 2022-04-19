/* Functions have been copied from mbedts/include/psa_util.h */

/*
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef _PSA_UTIL_H_
#define _PSA_UTIL_H_

psa_status_t mbedtls_to_psa_error( int ret );

int mbedtls_psa_err_translate_pk( psa_status_t status );

mbedtls_ecp_group_id xMbedtlsEccGroupIdFromPsaFamily( psa_ecc_family_t curve,
                                                      size_t bits );

int mbedtls_psa_get_ecc_oid_from_id( psa_ecc_family_t curve,
                                     size_t bits,
                                     char const ** oid,
                                     size_t * oid_len );

int pk_ecdsa_sig_asn1_from_psa( unsigned char * sig,
                                size_t * sig_len,
                                size_t buf_len );

psa_algorithm_t mbedtls_psa_translate_md( mbedtls_md_type_t md_alg );

psa_ecc_family_t xPsaFamilyFromMbedtlsEccGroupId( mbedtls_ecp_group_id xGroupId );

#endif /* _PSA_UTIL_H_ */
