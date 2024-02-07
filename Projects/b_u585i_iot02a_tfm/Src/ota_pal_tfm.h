/*
 * ota_pal_tfm.h
 *
 *  Created on: Feb 2, 2024
 *      Author: kanherea
 */

#ifndef OTA_PAL_TFM_H_
#define OTA_PAL_TFM_H_


#ifdef TFM_PSA_API

#define OTA_FILE_SIG_KEY_STR_MAX_LENGTH    32 /*!< Maximum length of the file signature key. */

/**
 * @ingroup ota_struct_types
 * @brief Application version structure.
 *
 */
typedef struct
{
    /* MISRA Ref 19.2.1 [Unions] */
    /* More details at: https://github.com/aws/ota-for-aws-iot-embedded-sdk/blob/main/MISRA.md#rule-192 */
    /* coverity[misra_c_2012_rule_19_2_violation] */
    union
    {
        #if ( defined( __BYTE_ORDER__ ) && defined( __ORDER_LITTLE_ENDIAN__ ) && ( __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ) ) || ( __little_endian__ == 1 ) || WIN32 || ( __BYTE_ORDER == __LITTLE_ENDIAN )
            struct version
            {
                uint16_t build; /*!< @brief Build of the firmware (Z in firmware version Z.Y.X). */
                uint8_t minor;  /*!< @brief Minor version number of the firmware (Y in firmware version Z.Y.X). */

                uint8_t major;  /*!< @brief Major version number of the firmware (X in firmware version Z.Y.X). */
            } x;                /*!< @brief Version number of the firmware. */
        #elif ( defined( __BYTE_ORDER__ ) && defined( __ORDER_BIG_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ ) || ( __big_endian__ == 1 ) || ( __BYTE_ORDER == __BIG_ENDIAN )
            struct version
            {
                uint8_t major;  /*!< @brief Major version number of the firmware (X in firmware version X.Y.Z). */
                uint8_t minor;  /*!< @brief Minor version number of the firmware (Y in firmware version X.Y.Z). */

                uint16_t build; /*!< @brief Build of the firmware (Z in firmware version X.Y.Z). */
            } x;                /*!< @brief Version number of the firmware. */
        #else /* if ( defined( __BYTE_ORDER__ ) && defined( __ORDER_LITTLE_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ) || ( __little_endian__ == 1 ) || WIN32 || ( __BYTE_ORDER == __LITTLE_ENDIAN ) */
        #error "Unable to determine byte order!"
        #endif /* if ( defined( __BYTE_ORDER__ ) && defined( __ORDER_LITTLE_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ) || ( __little_endian__ == 1 ) || WIN32 || ( __BYTE_ORDER == __LITTLE_ENDIAN ) */
        uint32_t unsignedVersion32;
        int32_t signedVersion32;
    } u; /*!< @brief Version based on configuration in big endian or little endian. */
} AppVersion32_t;
#endif



#endif /* OTA_PAL_TFM_H_ */
