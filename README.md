# FreeRTOS STM32U5 IoT Reference

This project demonstrates how to integrate modular [FreeRTOS kernel](https://www.freertos.org/RTOS.html) and [libraries](https://www.freertos.org/libraries/categories.html) with hardware enforced security to build more secure updatable cloud connected applications. The project is configured to run on the STM32U585 IoT Discovery Kit and connect to AWS IoT services.

The *Projects* directory consists of a [Non-TrustZone](Projects/b_u585i_iot02a_ntz) and a [Trusted-Firmware-M-Enabled](Projects/b_u585i_iot02a_tfm) project which both demonstrate connecting to AWS IoT Core and utilizing many of the services available via the MQTT protocol.

Refer to the [ Getting Started Guide ](Getting_Started_Guide.md) for step by step instructions on setting up your development environment.

This includes demonstration tasks for the following AWS services:
* [AWS IoT Device Shadow](https://docs.aws.amazon.com/iot/latest/developerguide/iot-device-shadows.html)
* [AWS IoT Device Defender](https://docs.aws.amazon.com/iot/latest/developerguide/device-defender.html)
* [AWS IoT Jobs](https://docs.aws.amazon.com/iot/latest/developerguide/iot-jobs.html)
* [MQTT File Delivery](https://docs.aws.amazon.com/iot/latest/developerguide/mqtt-based-file-delivery.html)
* [AWS IoT OTA Update](https://docs.aws.amazon.com/freertos/latest/userguide/freertos-ota-dev.html)

The demo projects both connect to AWS IoT core via the included Wi-Fi module and use the [CoreMQTT-Agent](https://github.com/FreeRTOS/coreMQTT-Agent) library to share a single MQTT connection among multiple tasks. These tasks publish environemnt and motion sensor data from a subset of the sensor available on the development board, and demonstrate use of the AWS IoT Device Shadow and Device Defender services.

## Hardware Description
The [STM32U585 IoT Discovery Kit](https://www.st.com/en/evaluation-tools/b-u585i-iot02a.html) integrates an [STM32U585AII6Q](https://www.st.com/en/microcontrollers-microprocessors/stm32u585ai.html) ARM Cortex-M33 microcontroller with 2048 KB of internal flash memory, 786 kB of SRAM, and the latest in security features. 

### STM32U585 IoT Discovery kit Resources
For more information on the STM32U585 IoT Discovery Kit and B-U585I-IOT02A development board, please refer to the following resources:
* [B-U585I-IOT02A Product Page](https://www.st.com/en/evaluation-tools/b-u585i-iot02a.html)
* [B-U585I-IOT02A Product Specification](https://www.st.com/resource/en/data_brief/b-u585i-iot02a.pdf)
* [B-U585I-IOT02A User Manual](https://www.st.com/resource/en/user_manual/um2839-discovery-kit-for-iot-node-with-stm32u5-series-stmicroelectronics.pdf)
* [B-U585I-IOT02A Schematic](https://www.st.com/resource/en/schematic_pack/mb1551-u585i-c02_schematic.pdf)

### STM32U5 Microcontroller Resources
For more details about the STM32U5 series of microcontrollers, please refer to the following resources:
* [STM32U5 Series Product Page](https://www.st.com/en/microcontrollers-microprocessors/stm32u5-series.html)
* [STM32U585xx Datasheet](https://www.st.com/resource/en/datasheet/stm32u585ai.pdf)
* [RM0456 STM32U575/575 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0456-stm32u575585-armbased-32bit-mcus-stmicroelectronics.pdf)
* [STM32U575xx and STM32U585xx Device Errata ](https://www.st.com/resource/en/errata_sheet/es0499-stm32u575xx-and-stm32u585xx-device-errata-stmicroelectronics.pdf)

## AWS IoT Core Demo Tasks
* MQTT Agent
* IoT Defender
* OTA Update
* Environment Sensor Publishing
* Motion Sensor Publishing

## Key Software Components

### LWIP TCP/IP Stack

### Mbedtls 3.1.0 TLS and Cryptography library

### Logging

### Command Line Interface (CLI)

### Key-Value Store
The key-value store located in the Common/kvstore directory is used to store runtime configuration values in non-volatile flash memory.
See [Common\kvstore](Common\kvstore\ReadMe.md) for details.

### PkiObject API
The PkiObject API takes care of some of the mundane tasks in converting between different representations of cryptographic objects such as public keys, private keys, and certificates.

Files located in the `Common/crypto` directory belong to the PkiObject module.

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

### Other Unix-like utilities
The following other utilities are also available in this image:

```
ps
    List the status of all running tasks and related runtime statistics.

kill
    kill [ -SIGNAME ] <Task ID>
        Signal a task with the named signal and the specified task id.

    kill [ -n ] <Task ID>
        Signal a task with the given signal number and the specified task id.

killall
    killall [ -SIGNAME ] <Task Name>
    killall [ -n ] <Task Name>
        Signal a task with a given name with the signal number or signal name given.

heapstat
    heapstat [-b | --byte]
        Display heap statistics in bytes.

    heapstat -h | -k | --kibi
        Display heap statistics in Kibibytes (KiB).

    heapstat -m | --mebi
        Display heap statistics in Mebibytes (MiB).

    heapstat --kilo
        Display heap statistics in Kilobytes (KB).

    heapstat --mega
        Display heap statistics in Megabytes (MB).

reset
    Reset (reboot) the system.

uptime
    Display system uptime.

rngtest <number of bytes>
    Read the specified number of bytes from the rng and output them base64 encoded.

assert
   Cause a failed assertion.
```

### Mbedtls Transport
The *Common/net/mbedtls_transport.c* file contains a transport layer implementation for coreMQTT and coreHTTP which uses mbedtls to encrypt the connection in a way supported by AWS IoT Core.

Optionally, client key / certificate authentication may be used with the mbedtls transport or this parameter may be set to NULL if not needed.

# Component Licensing

Source code located in the *Projects*, *Common*, *Middleware/AWS*, and *Middleware/FreeRTOS* directories are available under the terms of the MIT License. See the LICENSE file for more details.

Other libraries located in the *Drivers* and *Middleware* directories are available under the terms specified in each source file.

## FreeRTOS and AWS Libraries
All of the AWS and FreeRTOS libraries listed below are available under the MIT license.

| Library           | Path                                  | SPDX-License-Identifier |
| ----              | ----                                  | ----|
| Device Defender   | Middleware/AWS/IoTDeviceDefender      | MIT |
| Device Shadow     | Middleware/AWS/IoTDeviceShadow        | MIT |
| Jobs              | Middleware/AWS/IoTJobs                | MIT |
| FreeRTOS OTA      | Middleware/AWS/OTA                    | MIT |
| Backoff Algorithm | Middleware/FreeRTOS/backoffAlgorithm  | MIT |
| coreHTTP          | Middleware/FreeRTOS/coreHTTP          | MIT |
| coreJSON          | Middleware/FreeRTOS/coreJSON          | MIT |
| coreMQTT          | Middleware/FreeRTOS/coreMQTT          | MIT |
| coreMQTT-Agent    | Middleware/FreeRTOS/coreMQTT-Agent    | MIT |
| corePKCS11        | Middleware/FreeRTOS/corePKCS11        | MIT |
| Integration Tests | Middleware/FreeRTOS/integration_tests | MIT |
| FreeRTOS Kernel   | Middleware/FreeRTOS/kernel            | MIT |

## 3rd Party Libraries
3rd party libraries are available under a variety of licenses listed below:

| Library                       | Path                              | SPDX-License-Identifier       |
| ----                          | ----                              | ----                          |
| CMSIS-STM32U5                 | Drivers/CMSIS/Device/ST/STM32U5xx | [Apache-2.0](www.apache.org/licenses/LICENSE-2.0) |
| STM32U5 HAL                   | Drivers/STM32U5_HAL               | [BSD-3-Clause](https://www.opensource.org/licenses/BSD-3-Clause) |
| STM32U5 BSP Components        | Drivers/BSP/Components            | [BSD-3-Clause](https://www.opensource.org/licenses/BSD-3-Clause) |
| STM32U5 BSP B-U585I-IOT02A    | Drivers/BSP/B-U585I-IOT02A        | [BSD-3-Clause](https://www.opensource.org/licenses/BSD-3-Clause) |
| STM32U5 Mbedtls Alt Library   | Drivers/stm32u5_mbedtls_accel     | [Apache-2.0](https://github.com/Mbed-TLS/mbedtls/blob/master/LICENSE) |
| ARM CMSIS                     | Drivers/CMSIS/Core/               | [Apache-2.0](www.apache.org/licenses/LICENSE-2.0) |
| littlefs                      | Middleware/ARM/littlefs           | [BSD-3-Clause](https://github.com/littlefs-project/littlefs/blob/master/LICENSE.md)  |
| mbedtls                       | Middleware/ARM/mbedtls            | [Apache-2.0](https://github.com/Mbed-TLS/mbedtls/blob/master/LICENSE) |
| mcuboot                       | Middleware/ARM/mcuboot            | [Apache-2.0](https://github.com/mcu-tools/mcuboot/blob/master/LICENSE) |
| ota-pal-psa                   | Middleware/ARM/ota-pal-psa        | [MIT](https://github.com/Linaro/freertos-ota-pal-psa/blob/main/License.md) |
| ARM Trusted Firmware M        | Middleware/ARM/trusted-firmware-m | [BSD-3-Clause](https://github.com/paulbartell/tfm-staging/blob/f19c7be12f0ade301aa7d873fc7a48b93e193d64/license.rst) |
| http-parser                   | Middleware/http-parser            | [MIT](https://github.com/nodejs/http-parser/blob/main/LICENSE-MIT) |
| lwip                          | Middleware/lwip                   | [BSD-3-Clause](https://github.com/lwip-tcpip/lwip/blob/master/COPYING) |
| tinycbor                      | Middleware/tinycbor               | [MIT](https://github.com/intel/tinycbor/blob/main/LICENSE) |
| pkcs11.h (from p11-kit)       | Middleware/pkcs11/pkcs11.h        | [FSFULLR](Middleware/pkcs11/pkcs11.h), [BSD-3-Clause](https://github.com/p11-glue/p11-kit/blob/master/COPYING) |
| unity                         | Middleware/unity                  | [MIT](https://github.com/ThrowTheSwitch/Unity/blob/master/LICENSE.txt) |
