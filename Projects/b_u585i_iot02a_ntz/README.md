# Non-TrustZone Demo Project
The following Readme.md contains instructions on getting the non-trustzone (b_u585i_iot02a_ntz) version of the project up and running. It connects to AWS IoT Core and publishes sensor data.

## Software Components

### Littlefs
The littlefs library is used as a flash filesystem to demonstrate the external Octal-SPI NOR flash available on the b_u585i_iot02a board.

The littlefs port for this board can be found in the [Src/fs](Src/fs) directory.

### CorePKCS11
The CorePKCS11 library is used to ease integration and simulate the PKCS11 API that a secure element sdk might provide by handling cryptographic operations with mbedtls.

The CorePKSA11 PAL implementation that is used in the port can be found in the [Src/crypto/core_pkcs11_pal_littlefs.c](Src/crypto/core_pkcs11_pal_littlefs.c) file.

### FreeRTOS OTA Platform Abstraction Layer implementation
The OTA PAL is implemented to leverage the dual bank architecture. The internal flash memory is divided into two banks of size 1MB each, an active bank and a non-active bank. The bank split is achieved by setting the Dual Bank Option Byte Register to the enabled state. The loader starts the execution of the image from a well-defined start address in the flash layout. At factory reset, a valid image is programmed into the first bank and the bank is mapped to the start address known to the loader. During an OTA Update, the new firmware image is first staged to the second bank by the current program, running on the active bank. This is achieved by the dual bank feature of the platform, which allows execution in place from one bank while programming flash on the other bank. Once the new image is staged and its signature is verified, the current firmware swaps the bank by oggling the SWAP_BANK option byte register. Toggling the SWAP_BANK register remaps the second bank transparently onto the starting address already known to the loader. On the next reset, the loader starts execution of the new image from the second bank and tests that the image is good and can connect to AWS IoT core.

## Importing the projects into STM32CubeIDE

The b_u585i_iot02a_ntz project does not use the TrustZone capabilities of the STM32U5 microcontroller.

Follow the instructions in the repository [README.md](../../README.md) to import the Projects into STM32CubeIDE.

## Building and Flashing the Firmware Image

After importing the b_u585i_iot02a_ntz project into STM32CubeIDE, Build the project by highlighting it in the *Project Exporer* pane and then clicking **Project -> Build Project** from the menu at the top of STM32CubeIDE.

To write the newly built image to flash, select the Run As button, then select the `Flash_ntz` target.

## Flashing the Image from the commandline

Check that the STM32CProgrammer_CLI binary is in your PATH and run the following:
```
# Add tools directory to your PATH
source tools/env_setup.sh

# Change to the project directory
cd Projects/b_u585i_iot02a_ntz

# Run the flashing script.
stm32u5_tool.sh flash_ntz
```

## Performing Over-the-air (OTA) Firmware Update

The project shows an IoT reference implementation of how to integrate FreeRTOS libraries on STM32U5 platform to perform OTA update with AWS IoT using the *non trustzone* hardware capabilities.

The demo runs FreeRTOS OTA agent as one of the RTOS tasks in background, which waits for OTA updates from cloud.  

The non-trustzone version of the demo leverages the dual-bank architecture of the internal flash memory. The 2MB internal flash is split into two banks of 1MB each.

While the main firmware is running on one bank, an ota update is installed on the second bank.

## Performing Integration Test

Integration test is run when any of the execution parameter is enabled in [test_execution_config.h](../../Common/config/test_execution_config.h).

### Prerequisite

- Run [OTA](#performing-over-the-air-ota-firmware-update) once manually.
- Set [TEST_AUTOMATION_INTEGRATION](../../Common/config/ota_config.h) to 1.

### Steps for each test case

1. DEVICE_ADVISOR_TEST_ENABLED - device advisor test
    - Set DEVICE_ADVISOR_TEST_ENABLED to 1 in [test_execution_config.h](../../Common/config/test_execution_config.h).
    - Create a device advisor test on website. ( Iot Console -> Test -> Device Advisor )
    - Create test suite.
    - Run test suite and set the device advisor endpoint to MQTT_SERVER_ENDPOINT in [test_param_config.h](../../Common/config/test_param_config.h).
    - Set MQTT_SERVER_PORT and IOT_THING_NAME (Same as provisioned one) in [test_param_config.h](../../Common/config/test_param_config.h).
    - Build and run.
    - See device advisor test result on website.
1. MQTT_TEST_ENABLED - MQTT test
    - Set MQTT_TEST_ENABLED to 1 in [test_execution_config.h](../../Common/config/test_execution_config.h).
    - Set the MQTT endpoint to MQTT_SERVER_ENDPOINT in [test_param_config.h](../../Common/config/test_param_config.h).
    - Set MQTT_SERVER_PORT and IOT_THING_NAME (Same as provisioned one) in [test_param_config.h](../../Common/config/test_param_config.h).
    - Build and run.
    - See test result on target output.
    - Example output
        ```
        <INF>    14252 [QualTest  ] ---------STARTING TESTS--------- (qualification_app_main.c:101)
        ...
        <INF>    85259 [QualTest  ]  (qualification_app_main.c:101)
        <INF>    86259 [QualTest  ] ----------------------- (qualification_app_main.c:101)
        <INF>    87259 [QualTest  ] 7 Tests 0 Failures 0 Ignored  (qualification_app_main.c:101)
        <INF>    88259 [QualTest  ] OK (qualification_app_main.c:101)
        <INF>    89259 [QualTest  ] -------ALL TESTS FINISHED------- (qualification_app_main.c:101)
        <INF>    90259 [QualTest  ] End qualification test. (qualification_app_main.c:446)
        ```
1. TRANSPORT_INTERFACE_TEST_ENABLED - Transport layer test
    - Set TRANSPORT_INTERFACE_TEST_ENABLED to 1 [test_execution_config.h](../../Common/config/test_execution_config.h).
    - Follow [Run The Transport Interface Test](https://github.com/FreeRTOS/FreeRTOS-Libraries-Integration-Tests/tree/main/src/transport_interface#6-run-the-transport-interface-test) to start a echo server.
    - Set ECHO_SERVER_ENDPOINT / ECHO_SERVER_PORT / ECHO_SERVER_ROOT_CA / TRANSPORT_CLIENT_CERTIFICATE and TRANSPORT_CLIENT_PRIVATE_KEY in [test_param_config.h](../../Common/config/test_param_config.h).
    - Build and run.
    - See test result on target output.
    - Example output
        ```
        <INF>    15063 [QualTest  ] ---------STARTING TESTS--------- (qualification_app_main.c:102)
        ...
        <INF>   581023 [QualTest  ]  (qualification_app_main.c:102)
        <INF>   582023 [QualTest  ] ----------------------- (qualification_app_main.c:102)
        <INF>   583023 [QualTest  ] 14 Tests 0 Failures 0 Ignored  (qualification_app_main.c:102)
        <INF>   584023 [QualTest  ] OK (qualification_app_main.c:102)
        <INF>   585023 [QualTest  ] -------ALL TESTS FINISHED------- (qualification_app_main.c:102)
        <INF>   586023 [QualTest  ] End qualification test. (qualification_app_main.c:437)
        ```
1. OTA_PAL_TEST_ENABLED - OTA PAL test
    - Set OTA_PAL_TEST_ENABLED to 1 [test_execution_config.h](../../Common/config/test_execution_config.h).
    - Set OTA_PAL_FIRMWARE_FILE to "b_u585i_iot02a_ntz.bin" in [test_param_config.h](../../Common/config/test_param_config.h).
    - Build and run.
    - See test result on target output.
    - Example output
        ```
        <INF>    13698 [QualTest  ] ---------STARTING TESTS--------- (qualification_app_main.c:103)
        ...
        <INF>    32881 [QualTest  ] ----------------------- (qualification_app_main.c:103)
        <INF>    33881 [QualTest  ] 14 Tests 0 Failures 0 Ignored  (qualification_app_main.c:103)
        <INF>    34881 [QualTest  ] OK (qualification_app_main.c:103)
        <INF>    35881 [QualTest  ] -------ALL TESTS FINISHED------- (qualification_app_main.c:103)
        <INF>    36881 [QualTest  ] End qualification test. (qualification_app_main.c:438)
        ```
1. CORE_PKCS11_TEST_ENABLED - Core PKCS11 test
    - Set CORE_PKCS11_TEST_ENABLED to 1 [test_execution_config.h](../../Common/config/test_execution_config.h).
    - Build and run.
    - See test result on target output.
    - Example output
        ```
        <INF>    15410 [QualTest  ] ---------STARTING TESTS--------- (qualification_app_main.c:103)
        ...
        <INF>    41139 [QualTest  ] ----------------------- (qualification_app_main.c:103)
        <INF>    42139 [QualTest  ] 17 Tests 0 Failures 0 Ignored  (qualification_app_main.c:103)
        <INF>    43139 [QualTest  ] OK (qualification_app_main.c:103)
        <INF>    44139 [QualTest  ] -------ALL TESTS FINISHED------- (qualification_app_main.c:103)
        <INF>    45139 [QualTest  ] End qualification test. (qualification_app_main.c:438)
        ```
1. OTA_E2E_TEST_ENABLED - OTA E2E test
    - Disable all configurations in [test_execution_config.h](../../Common/config/test_execution_config.h).
    - Follow [FreeRTOS IDT 2.0](https://docs.aws.amazon.com/freertos/latest/userguide/lts-idt-freertos-qualification.html) to set-up tool.
    - Run IDT OTA E2E test cases.
    - See test result on tool output.
