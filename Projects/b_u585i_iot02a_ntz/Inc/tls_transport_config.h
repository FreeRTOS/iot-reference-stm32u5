#ifndef TLS_TRANSPORT_CONFIG
#define TLS_TRANSPORT_CONFIG

#include "core_pkcs11_config.h"

#define configTLS_MAX_LABEL_LEN    pkcs11configMAX_LABEL_LENGTH
#define TLS_KEY_PRV_LABEL          pkcs11_TLS_KEY_PRV_LABEL
#define TLS_KEY_PUB_LABEL          pkcs11_TLS_KEY_PUB_LABEL
#define TLS_CERT_LABEL             pkcs11_TLS_CERT_LABEL
#define TLS_ROOT_CA_CERT_LABEL     pkcs11_ROOT_CA_CERT_LABEL
#define OTA_SIGNING_KEY_LABEL      pkcs11configLABEL_CODE_VERIFICATION_KEY

/*
 * Define MBEDTLS_TRANSPORT_PKCS11 to enable certificate and key storage via the PKCS#11 API.
 */
#define MBEDTLS_TRANSPORT_PKCS11

/*
 * Define MBEDTLS_TRANSPORT_PSA to enable certificate and key storage via the ARM PSA API.
 */
/*#define MBEDTLS_TRANSPORT_PSA */


#endif /* TLS_TRANSPORT_CONFIG */
