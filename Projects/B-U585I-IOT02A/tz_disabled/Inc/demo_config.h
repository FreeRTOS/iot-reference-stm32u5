/*
 * Lab-Project-coreMQTT-Agent 201206
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 */

#ifndef DEMO_CONFIG_H
#define DEMO_CONFIG_H

#include "logging_levels.h"

#ifndef LOG_LEVEL
    #define LOG_LEVEL                               LOG_DEBUG
#endif

#include "logging.h"

/************ End of logging configuration ****************/



/* Constants that select which demos to build into the project:
 * Set the following to 1 to include the demo in the build, or 0 to exclude the
 * demo. */
#define democonfigCREATE_LARGE_MESSAGE_SUB_PUB_TASK        0
#define democonfigLARGE_MESSAGE_SUB_PUB_TASK_STACK_SIZE    ( configMINIMAL_STACK_SIZE )

#define democonfigNUM_SIMPLE_SUB_PUB_TASKS_TO_CREATE       1
#define democonfigSIMPLE_SUB_PUB_TASK_STACK_SIZE           ( 2048 )

#define democonfigCREATE_CODE_SIGNING_OTA_DEMO             0
#define democonfigCODE_SIGNING_OTA_TASK_STACK_SIZE         ( configMINIMAL_STACK_SIZE )


#define democonfigCREATE_DEFENDER_DEMO                     0
#define democonfigDEFENDER_TASK_STACK_SIZE                 ( configMINIMAL_STACK_SIZE )

#define democonfigCREATE_SHADOW_DEMO                       0
#define democonfigSHADOW_TASK_STACK_SIZE                   ( configMINIMAL_STACK_SIZE )

/**
 * @brief Server's root CA certificate.
 *
 * For AWS IoT MQTT broker, this certificate is used to identify the AWS IoT
 * server and is publicly available. Refer to the AWS documentation available
 * in the link below.
 * https://docs.aws.amazon.com/iot/latest/developerguide/server-authentication.html#server-authentication-certs
 *
 * @note This certificate should be PEM-encoded.
 *
 * @note If you would like to setup an MQTT broker for running this demo,
 * please see `mqtt_broker_setup.txt`.
 *
 * Must include the PEM header and footer:
 * "-----BEGIN CERTIFICATE-----\n"\
 * "...base64 data...\n"\
 * "-----END CERTIFICATE-----\n"
 *
 * #define democonfigROOT_CA_PEM    "...insert here..."
 */
#define democonfigROOT_CA_PEM                              "-----BEGIN CERTIFICATE-----\n\
MIIBtjCCAVugAwIBAgITBmyf1XSXNmY/Owua2eiedgPySjAKBggqhkjOPQQDAjA5\n\
MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6b24g\n\
Um9vdCBDQSAzMB4XDTE1MDUyNjAwMDAwMFoXDTQwMDUyNjAwMDAwMFowOTELMAkG\n\
A1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJvb3Qg\n\
Q0EgMzBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABCmXp8ZBf8ANm+gBG1bG8lKl\n\
ui2yEujSLtf6ycXYqm0fc4E7O5hrOXwzpcVOho6AF2hiRVd9RFgdszflZwjrZt6j\n\
QjBAMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgGGMB0GA1UdDgQWBBSr\n\
ttvXBp43rDCGB5Fwx5zEGbF4wDAKBggqhkjOPQQDAgNJADBGAiEA4IWSoxe3jfkr\n\
BqWTrBqYaGFy+uGh0PsceGCmQ5nFuMQCIQCcAu/xlJyzlvnrxir4tiz+OpAUFteM\n\
YyRIHN8wfdVoOw==\n\
-----END CERTIFICATE-----"

/**
 * @brief Client certificate.
 *
 * For AWS IoT MQTT broker, refer to the AWS documentation below for details
 * regarding client authentication.
 * https://docs.aws.amazon.com/iot/latest/developerguide/client-authentication.html
 *
 * @note This certificate should be PEM-encoded.
 *
 * Must include the PEM header and footer:
 * "-----BEGIN CERTIFICATE-----\n"\
 * "...base64 data...\n"\
 * "-----END CERTIFICATE-----\n"
 *
 * #define democonfigCLIENT_CERTIFICATE_PEM    "...insert here..."
 */
#define democonfigCLIENT_CERTIFICATE_PEM                   "-----BEGIN CERTIFICATE-----\n\
MIIDWTCCAkGgAwIBAgIUSMYbzEptnfB4dbPP3VQE2St0jC4wDQYJKoZIhvcNAQEL\n\
BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g\n\
SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTIxMDcyNzIzMDMx\n\
OFoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0\n\
ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKTrTumXyNDHFF/qsLci\n\
25OhdpM/BSkDiB+ZO9AZViY9hnMfB7OXqtLTpB6a+WOLZwWcHtTbhiI+bZyZqRrq\n\
8xau1L7xtEqZlNRMXheWqNkvWJVYilmCGN4xmOTfv882f7jQbL4yOVyjuP2ROoDc\n\
rl2ofE4UCIJWxDZNDIUWwBrhC8/6IXjBLKBMtvjAYReIRZatOBvGUR7++TYLgcL8\n\
3+g+B1ybqJ/Vxvsr1ygUapHWxkZj6t9E/YrtoxmZoF51M+kcPq9WEtHxVDxlZg8j\n\
WgCzkwqn+XbswegiX6ocXYbf70o2VomPLjvYo/pJtL71/DAxBLb1iohN0wn2/lSi\n\
AR0CAwEAAaNgMF4wHwYDVR0jBBgwFoAUMk6pVM44agDHU3tDQv5aK0crrhowHQYD\n\
VR0OBBYEFP8hfnVldwP0PxhgPGIpZ7gVEMTJMAwGA1UdEwEB/wQCMAAwDgYDVR0P\n\
AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQDObemaWkGFsAP+ymgLSWWMFXbO\n\
oPh5TpcJyVvAs80b72v8ge+ThQG6qD3hy4mM/Jb8U6eg5KaTLfdIH8HzaLicMx28\n\
E0PZGpVcsePlBw8TdxN+syS35/WtMuIaTT5sSxoXyEngULo8MIkOqetMqHHA1tYh\n\
HnGls+tWWWdNAEpUTvjoikzAl5f20KYy/sgDfPiHRcrjRbKdWgVI4RREaAojzAcq\n\
uZxn0TaB/dh6QOy1F9eNyDHwXH3OcoFXMpFTsx6DJUZgNAX0EEMauliWmN1CGM79\n\
dlO6EIiVqtyKEymJEsxThbQtYrGblfhjm8jyTlPcngzwBLsN5R0XttHYQa7t\n\
-----END CERTIFICATE-----"

/**
 * @brief Client's private key.
 *
 *!!! Please note pasting a key into the header file in this manner is for
 *!!! convenience of demonstration only and should not be done in production.
 *!!! Never paste a production private key here!.  Production devices should
 *!!! store keys securely, such as within a secure element.  Additionally,
 *!!! we provide the corePKCS library that further enhances security by
 *!!! enabling securely stored keys to be used without exposing them to
 *!!! software.
 *
 * For AWS IoT MQTT broker, refer to the AWS documentation below for details
 * regarding clientauthentication.
 * https://docs.aws.amazon.com/iot/latest/developerguide/client-authentication.html
 *
 * @note This private key should be PEM-encoded.
 *
 * Must include the PEM header and footer:
 * "-----BEGIN RSA PRIVATE KEY-----\n"\
 * "...base64 data...\n"\
 * "-----END RSA PRIVATE KEY-----\n"
 *
 * #define democonfigCLIENT_PRIVATE_KEY_PEM    "...insert here..."
 */
#define democonfigCLIENT_PRIVATE_KEY_PEM                   "-----BEGIN RSA PRIVATE KEY-----\n\
MIIEowIBAAKCAQEApOtO6ZfI0McUX+qwtyLbk6F2kz8FKQOIH5k70BlWJj2Gcx8H\n\
s5eq0tOkHpr5Y4tnBZwe1NuGIj5tnJmpGurzFq7UvvG0SpmU1ExeF5ao2S9YlViK\n\
WYIY3jGY5N+/zzZ/uNBsvjI5XKO4/ZE6gNyuXah8ThQIglbENk0MhRbAGuELz/oh\n\
eMEsoEy2+MBhF4hFlq04G8ZRHv75NguBwvzf6D4HXJuon9XG+yvXKBRqkdbGRmPq\n\
30T9iu2jGZmgXnUz6Rw+r1YS0fFUPGVmDyNaALOTCqf5duzB6CJfqhxdht/vSjZW\n\
iY8uO9ij+km0vvX8MDEEtvWKiE3TCfb+VKIBHQIDAQABAoIBAHYFPy/Dw55FGbua\n\
hGRKucBoqCavzs2PDXcvfbJqr1Amb3UrG6PWQhUmCCku1pH6TPuiwh2qC8+txVtw\n\
S6hLwzLUYsNSzaclSXC0RhGza5ohW89k0X1EsO8xpy0DQozTf4uO/IMQBiTZtaUg\n\
rTFSvCWiUXK+u2grF7eUZAVKRqf0hp2sK8V5MlMgYZW9TLC+st04/jh8h1e1yks1\n\
184EtJnF3lZoJVYMTuv53c83PEnB54SulY6w5DxHFYKcXof7TovTLE/ndhx114ou\n\
VUJj+GmqqJ9CtiQ0Ltvy9C6nYjYDA1hr/6boEvloN10ML23hHsIWE8fokpTkqN01\n\
3ofEkIECgYEA2cRSFO6YNUvcoNQbPIQQL9+Pp9MUUHDGwJa5IsfsaWwWW3i27fcr\n\
VNA0SSxEOmNaEJ7eTejIB4XYeOqVRlHugo4cOozpRgCYX89cQdYAuTZ3mSSoTkGv\n\
JhyMHdH8hvR03oWrYDn/wNymli8zDLoNHOIfEPmuM0aVh9aFYFdghMkCgYEAwd+2\n\
O6XXAOm+YNMaM6x+azHSU+fep0rIZmFbZMoLfgah8IMRwGk1R/Y2Z3D3lDJAncaq\n\
yJ+OCnMmZ1SgTbYC00otwcl0NmfpsLgy6ZLuNkElR0nTcqolukY9HAqVmrfbN750\n\
bev02zWE+VmfDHyOJlsqcHbiiv6bdrSNpywdp7UCgYB46DJ2or5poPQfa4SvxddC\n\
7UpScLSvsN35IfYqpHvTUIrdQJm4Z+psDalqEyTSkkT4Q2zELwGYg6zb/crhny3g\n\
2Mw9ie0ey8fOMlBT0WDXdLFUmvDDel6nt2PoTqV5vJKC1g5/v9QLHbd0XUJnx4ju\n\
R3HeN/KLT55ILEgjZIAXWQKBgQCBCEWfGVcpkmhUxOtxbOKOsZAMrAvyR8Fs8msD\n\
f716WSu6iWFtMBh4FpRK8FK23WNIHwtXj2nX5p+ushIm5nam7X3athuUgkB0j4PD\n\
FTlZ/q2y7p2+eSO3ADx3x0uet7M9PJL8/cfvhYVE97L1eoiZWp+6TkWkwKPzs2N7\n\
2c666QKBgChyQ61UoBQ9KNmW13CWt24HaHQBxjReejiPOgx5gc1/f89k0LK+mDF6\n\
5ho+zwBI8DEsRsODB7tk5Rq1C0YbcRVCJdM27/BCxg2Ofsc6cGJ4ifX7fVqlzxRI\n\
BDgKDNxI/MsSS3N06K/44DWfb8nAGN5Q7YwogmkQyP8s6UQJH1KA\n\
-----END RSA PRIVATE KEY-----"



/**
 * @brief An option to disable Server Name Indication.
 *
 * @note When using a local Mosquitto server setup, SNI needs to be disabled
 * for an MQTT broker that only has an IP address but no hostname. However,
 * SNI should be enabled whenever possible.
 */
#define democonfigDISABLE_SNI                ( pdFALSE )

/**
 * @brief Configuration that indicates if the demo connection is made to the AWS IoT Core MQTT broker.
 *
 * If username/password based authentication is used, the demo will use appropriate TLS ALPN and
 * SNI configurations as required for the Custom Authentication feature of AWS IoT.
 * For more information, refer to the following documentation:
 * https://docs.aws.amazon.com/iot/latest/developerguide/custom-auth.html#custom-auth-mqtt
 *
 * #define democonfigUSE_AWS_IOT_CORE_BROKER    ( 1 )
 */
#define democonfigUSE_AWS_IOT_CORE_BROKER    ( 1 )

/**
 * @brief The password value for authenticating client to the MQTT broker when
 * username/password based client authentication is used.
 *
 * For AWS IoT MQTT broker, refer to the AWS IoT documentation below for
 * details regarding client authentication with a username and password.
 * https://docs.aws.amazon.com/iot/latest/developerguide/custom-authentication.html
 * An authorizer setup needs to be done, as mentioned in the above link, to use
 * username/password based client authentication.
 *
 * #define democonfigCLIENT_PASSWORD    "...insert here..."
 */

/**
 * @brief The name of the operating system that the application is running on.
 * The current value is given as an example. Please update for your specific
 * operating system.
 */
#define democonfigOS_NAME                   "FreeRTOS"

/**
 * @brief The version of the operating system that the application is running
 * on. The current value is given as an example. Please update for your specific
 * operating system version.
 */
#define democonfigOS_VERSION                tskKERNEL_VERSION_NUMBER

/**
 * @brief The name of the hardware platform the application is running on. The
 * current value is given as an example. Please update for your specific
 * hardware platform.
 */
#define democonfigHARDWARE_PLATFORM_NAME    "WinSim"

/**
 * @brief The name of the MQTT library used and its version, following an "@"
 * symbol.
 */
#define democonfigMQTT_LIB                  "core-mqtt@1.0.0"

/**
 * @brief Whether to use mutual authentication. If this macro is not set to 1
 * or not defined, then plaintext TCP will be used instead of TLS over TCP.
 */
#define democonfigUSE_TLS                   1

/**
 * @brief Set the stack size of the main demo task.
 *
 * In the Windows port, this stack only holds a structure. The actual
 * stack is created by an operating system thread.
 */
#define democonfigDEMO_STACKSIZE            configMINIMAL_STACK_SIZE

/**********************************************************************************
* Error checks and derived values only below here - do not edit below here. -----*
**********************************************************************************/


#ifndef democonfigROOT_CA_PEM
    #error "Please define Root CA certificate of the MQTT broker(democonfigROOT_CA_PEM) in demo_config.h."
#endif

/*
 *!!! Please note democonfigCLIENT_PRIVATE_KEY_PEM in used for
 *!!! convenience of demonstration only.  Production devices should
 *!!! store keys securely, such as within a secure element.
 */

#ifndef democonfigCLIENT_CERTIFICATE_PEM
    #error "Please define client certificate(democonfigCLIENT_CERTIFICATE_PEM) in demo_config.h."
#endif
#ifndef democonfigCLIENT_PRIVATE_KEY_PEM
    #error "Please define client private key(democonfigCLIENT_PRIVATE_KEY_PEM) in demo_config.h."
#endif

#ifndef democonfigMQTT_BROKER_PORT
    #define democonfigMQTT_BROKER_PORT    ( 8883 )
#endif

/**
 * @brief ALPN (Application-Layer Protocol Negotiation) protocol name for AWS IoT MQTT.
 *
 * This will be used if democonfigMQTT_BROKER_PORT is configured as 443 for the AWS IoT MQTT broker.
 * Please see more details about the ALPN protocol for AWS IoT MQTT endpoint
 * in the link below.
 * https://aws.amazon.com/blogs/iot/mqtt-with-tls-client-authentication-on-port-443-why-it-is-useful-and-how-it-works/
 */
#define AWS_IOT_MQTT_ALPN           "\x0ex-amzn-mqtt-ca"

/**
 * @brief This is the ALPN (Application-Layer Protocol Negotiation) string
 * required by AWS IoT for password-based authentication using TCP port 443.
 */
#define AWS_IOT_CUSTOM_AUTH_ALPN    "\x04mqtt"

/**
 * Provide default values for undefined configuration settings.
 */
#ifndef democonfigOS_NAME
    #define democonfigOS_NAME    "FreeRTOS"
#endif

#ifndef democonfigOS_VERSION
    #define democonfigOS_VERSION    tskKERNEL_VERSION_NUMBER
#endif

#ifndef democonfigHARDWARE_PLATFORM_NAME
    #define democonfigHARDWARE_PLATFORM_NAME    "STM32U5 IoT Discovery"
#endif

#ifndef democonfigMQTT_LIB
    #define democonfigMQTT_LIB    "core-mqtt@1.0.0"
#endif

/**
 * @brief The MQTT metrics string expected by AWS IoT.
 */
#define AWS_IOT_METRICS_STRING                                 \
    "?SDK=" democonfigOS_NAME "&Version=" democonfigOS_VERSION \
    "&Platform=" democonfigHARDWARE_PLATFORM_NAME "&MQTTLib=" democonfigMQTT_LIB

/**
 * @brief The length of the MQTT metrics string expected by AWS IoT.
 */
#define AWS_IOT_METRICS_STRING_LENGTH    ( ( uint16_t ) ( sizeof( AWS_IOT_METRICS_STRING ) - 1 ) )

/**
 * @brief Length of client identifier.
 */
#define democonfigCLIENT_IDENTIFIER_LENGTH    ( ( uint16_t ) ( sizeof( democonfigCLIENT_IDENTIFIER ) - 1 ) )

/**
 * @brief Length of MQTT server host name.
 */
#define democonfigBROKER_ENDPOINT_LENGTH      ( ( uint16_t ) ( sizeof( democonfigMQTT_BROKER_ENDPOINT ) - 1 ) )


#endif /* DEMO_CONFIG_H */
