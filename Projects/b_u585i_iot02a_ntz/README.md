# Non-TrustZone Demo Project
The following Readme.md contains instructions on getting the non-trustzone (b_u585i_iot02a_ntz) version of the project up and running. It connects to AWS IoT Core and publishes sensor data.

[1 Software Components](#1-software-components)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[1.1 Littlefs](#11-littlefs)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[1.2 CorePKCS11](#12-corepkcs11)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[1.3 FreeRTOS OTA Platform Abstraction Layer implementation](#13-freertos-ota-platform-abstraction-layer-implementation)<br>
[2 Importing the projects into STM32CubeIDE](#2-importing-the-projects-into-stm32cubeide)<br>
[3 Building and Flashing the Firmware Image](#3-building-and-flashing-the-firmware-image)<br>
[4 Flashing the Image from the commandline](#4-flashing-the-image-from-the-commandline)<br>
[5 Performing Over-the-air (OTA) Firmware Update](#5-performing-over-the-air-ota-firmware-update)<br>
[6 Performing Integration Test](#6-performing-integration-test)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[6.1 Prerequisite](#61-prerequisite)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[6.2 Steps for each test case](#62-steps-for-each-test-case)<br>
[7 Run AWS IoT Device Tester](#7-run-aws-iot-device-tester)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[7.1 Prerequisite](#71-prerequisite)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[7.2 Download AWS IoT Device Tester](#72-download-aws-iot-device-tester)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[7.3 Configure AWS IoT Device Tester](#73-configure-aws-iot-device-tester)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[7.4 Running AWS IoT Device Tester](#74-running-aws-iot-device-tester)<br>

## 1 Software Components

### 1.1 Littlefs
The littlefs library is used as a flash filesystem to demonstrate the external Octal-SPI NOR flash available on the b_u585i_iot02a board.

The littlefs port for this board can be found in the [Src/fs](Src/fs) directory.

### 1.2 CorePKCS11
The CorePKCS11 library is used to ease integration and simulate the PKCS11 API that a secure element sdk might provide by handling cryptographic operations with mbedtls.

The CorePKSA11 PAL implementation that is used in the port can be found in the [Src/crypto/core_pkcs11_pal_littlefs.c](Src/crypto/core_pkcs11_pal_littlefs.c) file.

### 1.3 FreeRTOS OTA Platform Abstraction Layer implementation
The OTA PAL is implemented to leverage the dual bank architecture. The internal flash memory is divided into two banks of size 1MB each, an active bank and a non-active bank. The bank split is achieved by setting the Dual Bank Option Byte Register to the enabled state. The loader starts the execution of the image from a well-defined start address in the flash layout. At factory reset, a valid image is programmed into the first bank and the bank is mapped to the start address known to the loader. During an OTA Update, the new firmware image is first staged to the second bank by the current program, running on the active bank. This is achieved by the dual bank feature of the platform, which allows execution in place from one bank while programming flash on the other bank. Once the new image is staged and its signature is verified, the current firmware swaps the bank by oggling the SWAP_BANK option byte register. Toggling the SWAP_BANK register remaps the second bank transparently onto the starting address already known to the loader. On the next reset, the loader starts execution of the new image from the second bank and tests that the image is good and can connect to AWS IoT core.

## 2 Importing the projects into STM32CubeIDE

The b_u585i_iot02a_ntz project does not use the TrustZone capabilities of the STM32U5 microcontroller.

Follow the instructions in the repository [README.md](../../README.md) to import the Projects into STM32CubeIDE.

## 3 Building and Flashing the Firmware Image

After importing the b_u585i_iot02a_ntz project into STM32CubeIDE, Build the project by highlighting it in the *Project Exporer* pane and then clicking **Project -> Build Project** from the menu at the top of STM32CubeIDE.

To write the newly built image to flash, select the Run As button, then select the `Flash_ntz` target.

## 4 Flashing the Image from the commandline

Check that the STM32CProgrammer_CLI binary is in your PATH and run the following:
```
# Add tools directory to your PATH
source tools/env_setup.sh

# Change to the project directory
cd Projects/b_u585i_iot02a_ntz

# Run the flashing script.
stm32u5_tool.sh flash_ntz
```

## 5 Performing Over-the-air (OTA) Firmware Update

The project shows an IoT reference implementation of how to integrate FreeRTOS libraries on STM32U5 platform to perform OTA update with AWS IoT using the *non trustzone* hardware capabilities.

The demo runs FreeRTOS OTA agent as one of the RTOS tasks in background, which waits for OTA updates from cloud.  

The non-trustzone version of the demo leverages the dual-bank architecture of the internal flash memory. The 2MB internal flash is split into two banks of 1MB each.

While the main firmware is running on one bank, an ota update is installed on the second bank.

## 6 Performing Integration Test

Integration test is run when any of the execution parameter is enabled in [test_execution_config.h](../../Common/config/test_execution_config.h).

### 6.1 Prerequisite

- Run [OTA](#5-performing-over-the-air-ota-firmware-update) once manually.
- Set [TEST_AUTOMATION_INTEGRATION](../../Common/config/ota_config.h) to 1.

### 6.2 Steps for each test case

1. Device Advisor Test
    - Set DEVICE_ADVISOR_TEST_ENABLED to 1 in [test_execution_config.h](../../Common/config/test_execution_config.h).
    - Create a [device advisor test](https://docs.aws.amazon.com/iot/latest/developerguide/device-advisor.html) on website. ( Iot Console -> Test -> Device Advisor )
    - Create test suite.
    - Run test suite and set the device advisor endpoint to MQTT_SERVER_ENDPOINT in [test_param_config.h](../../Common/config/test_param_config.h).
    - Set MQTT_SERVER_PORT and IOT_THING_NAME (Same as provisioned one) in [test_param_config.h](../../Common/config/test_param_config.h).
    - Build and run.
    - See device advisor test result on website.
1. MQTT Test
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
1. Transport Interface Test
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
1. OTA PAL Test
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
1. Core PKCS11 Test
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

## 7 Run AWS IoT Device Tester

This repository can be tested using [AWS IoT Device Tester for FreeRTOS (IDT)](https://aws.amazon.com/freertos/device-tester/). IDT is a downloadable tool that can be used to exercise a device integration with FreeRTOS to validate functionality and compatibility with Amazon IoT cloud. Passing the test suite provided by IDT is also required to qualify a device for the [Amazon Partner Device Catalogue](https://devices.amazonaws.com/).

IDT runs a suite of tests that include testing the device's transport interface layer implementation, PKCS11 functionality, and OTA capabilities. In IDT test cases, the IDT binary will make a copy of the source code, update the header files in the project, then compile the project and flash the resulting image to your board. Finally, IDT will read serial output from the board and communicate with the AWS IoT cloud to ensure that test cases are passing.

### 7.1 Prerequisite
- Run [OTA](#5-performing-over-the-air-ota-firmware-update) once manually.
- Set [TEST_AUTOMATION_INTEGRATION](../../Common/config/ota_config.h) to 1.

### 7.2 Download AWS IoT Device Tester

The latest version of IDT can be downloaded from the [public documentation page](https://docs.aws.amazon.com/freertos/latest/userguide/dev-test-versions-afr.html). This repository only supports test suites FRQ_2.2.0 or later.

### 7.3 Configure AWS IoT Device Tester

After downloading and unzipping IDT onto your file system, you should extract a file structure that includes the following directories:

* The `bin` directory holds the devicetester binary, which is the entry point used to run IDT
* The `results` directory holds logs that are generated every time you run IDT.
* The `configs` directory holds configuration values that are needed to set up IDT

Before running IDT, the files in `configs` need to be updated. In this repository, we have pre-defined configs available in the `idt_config` directory. Copy these templates over into IDT, and the rest of this section will walk through the remaining values that need to be filled in.

First, copy one of each file from `idt_config` (based on host OS) in this reference repository to the `configs` directory inside the newly downloaded IDT project. This should provide you with the following files in `device_tester/configs` directory:

```
configs/dummyPublicKeyAsciiHex.txt
configs/flash.bat or flash.sh
configs/config.json
configs/userdata.json
configs/device.json
configs/build.bat or build.sh
```

Next, we need to update some configuration values in these files.

* In `build.bat` / `build.sh`, update ESP_IDF_PATH, and ESP_IDF_FRAMEWORK_PATH
* In `flash.bat` / `flash.sh`, update ESP_IDF_PATH, ESP_IDF_FRAMEWORK_PATH, and NUM_COMPORT

* In `config.json`, update the `profile` and `awsRegion` fields
* In `device.json`, update `serialPort` to the serial port of your board as from [PORT](./GettingStartedGuide.md#23-provision-the-esp32-c3-with-the-private-key-device-certificate-and-ca-certificate-in-development-mode). Update `publicKeyAsciiHexFilePath` to the absolute path to `dummyPublicKeyAsciiHex.txt`. Update `publicDeviceCertificateArn` to the ARN of the certificate uploaded when [Setup AWS IoT Core](./GettingStartedGuide.md#21-setup-aws-iot-core).
* In `userdata.json`, update `sourcePath` to the absolute path to the root of this repository.
* In `userdata.json`, update `signerCertificate` with the ARN of the [Setup pre-requisites for OTA cloud resources
.](./GettingStartedGuide.md#51-setup-pre-requisites-for-ota-cloud-resources)
* Run all the steps to create a [second code signing certificate](./GettingStartedGuide.md#51-setup-pre-requisites-for-ota-cloud-resources) but do NOT provision the key onto your board. Copy the ARN for this certificate in `userdata.json` for the field `untrustedSignerCertificate`.

### 7.4 Running AWS IoT Device Tester

With configuration complete, IDT can be run for an individual test group, a test case, or the entire qualification suite.

To list the available test groups, run:

```
.\devicetester_win_x86-64.exe list-groups
```

To run any one test group, run e.g.:

```
.\devicetester_win_x86-64.exe run-suite -g FullCloudIoT -g OTACore
```

To run the entire qualification suite, run:

```
.\devicetester_win_x86-64.exe run-suite
```

For more information, `.\devicetester_win_x86-64.exe help` will show all available commands.

When IDT is run, it generates the `results/uuid` directory that contains the logs and other information associated with your test run, allowing failures to easily be debugged.
