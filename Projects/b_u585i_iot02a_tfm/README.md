# TrustZone / TFM Enabled Demo Project
The b_u585i_iot02a_tfm project utilizes [ ARM Trusted Firmware M ](https://www.trustedfirmware.org/projects/tf-m/) as the TrustZone Secure Processing Environment firmware and bootloader.

In this project, Trusted Firmware-M is configured to use the [ Large Profile ](https://tf-m-user-guide.trustedfirmware.org/docs/technical_references/design_docs/profiles/tfm_profile_large.html) with [ Isolation Level 2 ](https://tf-m-user-guide.trustedfirmware.org/docs/technical_references/design_docs/ff_isolation.html). Isolation Level 3 support is likely to be implemented for the STM32U5 MCU at some point in the future.

TF-M utilizes the TrustZone features of the Cortex-M33 core to provide isolation between code running in the NSPE (Non Secure Processing Environment) amd code running in the Secure Processing Environment (SPE).
In addition, the secure region MPU is used to provide isolation between different portions of the SPE (Secure Processing Environment) firmware.

[1 Definitions](#1-definitions)<br>
[2 Features](#2-features)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[2.1 Secure Boot / Secure Firmware Update](#21-secure-boot--secure-firmware-update)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[2.2 Anti-Rollback](#22-anti-rollback)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[2.3 Cryptographic Operations](#23-cryptographic-operations)<br>
[3 Project Configuration](#3-project-configuration)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[3.1 Flash Protection Mechanisms](#31-flash-protection-mechanisms)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[3.2 Flash Memory Layout](#32-flash-memory-layout)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[3.3 Flash Option Byte Configuration](#33-flash-option-byte-configuration)<br>
[4 Building the Firmware Images](#4-building-the-firmware-images)<br>
[5 Customizing the Firmware Metadata for mcuboot](#5-customizing-the-firmware-metadata-for-mcuboot)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[5.1 Using custom firmware region Signing Keys](#51-using-custom-firmware-region-signing-keys)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[5.2 Setting the version number and Anti-Rollback counter for each image](#52-setting-the-version-number-and-anti-rollback-counter-for-each-image)<br>
[6 Build Artifacts](#6-build-artifacts)<br>
[7 Flashing the Image from STM32CubeIDE](#7-flashing-the-image-from-stm32cubeide)<br>
[8 Flashing the Image from the command line](#8-flashing-the-image-from-the-command-line)<br>
[9 Performing Over-the-air (OTA) Firmware Update](#9-performing-over-the-air-ota-firmware-update)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[9.1 Specificities compared to the description available in the Getting Started Guide](#91-specificities-compared-to-the-description-available-in-the-getting-started-guide)<br>
[10 Performing Integration Test](#10-performing-integration-test)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[10.1 Prerequisite](#101-prerequisite)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[10.2 Steps for each test case](#102-steps-for-each-test-case)<br>
[11 Run AWS IoT Device Tester](#11-run-aws-iot-device-tester)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[11.1 Prerequisite](#111-prerequisite)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[11.2 Download AWS IoT Device Tester](#112-download-aws-iot-device-tester)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[11.3 Configure AWS IoT Device Tester](#113-configure-aws-iot-device-tester)<br>
&nbsp;&nbsp;&nbsp;&nbsp;[11.4 Running AWS IoT Device Tester](#114-running-aws-iot-device-tester)<br>

## 1 Definitions
**NSPE**: Non Secure Processing Environment

**SPE**: Secure Processing Environment

**Secure Boot**: The combination of signed application images, an immutable public key(s), and an immutable bootloader which verifies the signature appended to each application image against the relevant public key(s) prior to installation and execution.

**PSA**: ARM's Platform Security Architecture

**PSA APIs**: ARM's [PSA Crypto](https://armmbed.github.io/mbed-crypto/html/), [FWU (Firmware Update)](https://developer.arm.com/documentation/ihi0093/0000/),

**PSA Root of Trust (PRoT)**: The security sensitive portions of the SPE firmware provided by Trusted Firmware M.

**Application Root of Trust (ARoT)**: The remaining portions of the SPE firmware provided by Trusted Firmware M and the application developer.

## 2 Features

### 2.1 Secure Boot / Secure Firmware Update
In this project, TF-M is primarily used primarily for secure boot, firmware update functionality, and private key operations (key generation and signing). The [ mcuboot ](https://www.mcuboot.com/) library forms a core part of the BL2 bootloader component of TF-M and forms the basis of the secure boot and firmware update implementation. Mcuboot defines the metadata format used for the header and trailer of each firmware image. This metadata include a digest (hash) of the firmware, cryptographic signature, version number, list of dependencies on other modules, and a variety of other flags. The BL2 bootloader verifies each image prior to every boot.

### 2.2 Anti-Rollback
Anti Rollback is achieved using the MCUboot and TF-M [ **Security Counter** ](https://tf-m-user-guide.trustedfirmware.org/docs/technical_references/design_docs/tfm_secure_boot.html) implementation.
For each region (SPE and NSPE), updates must have a security counter value which is greater than or equal to the current security counter for that region.

During an OTA update, an invalid image security counter will result in the update image will be rejected and the BL2 will revert to the previous image for that region. After an OTA update is booted and "confirmed" via a self-test, the corresponding security counter is incremented to prevent rollback during the next reset cycle. The FreeRTOS OTA implementation included in this project performs this reset automatically.

If a region is updated locally via JTAG (or another method which includes an image with the "confirmed" flag pre-set) to an image with a security counter less than the current security counter for that region, booting will fail.

### 2.3 Cryptographic Operations
Private Key operations are performed via calls to the [ ARM PSA Cryptography API ](https://armmbed.github.io/mbed-crypto/html). Functions that transition the microcontroller from NS mode to S mode (veneer functions) are tightly controlled and located in a specific region of program flash which is marked "Non Secure Callable". Generic NSPE code cannot cause a switch to S mode, but Non-Secure Callable veener functions may. Keys are stored in the TFM "Internal Trusted Storage" flash region, which is only accessible by the TFM Root of Trust region (isolation level 2) or TFM crypto service (isolation level 3).

Bulk symmetric crypto, signature verification, and key negotiation operations occur in the NSPE using the same version of the mbedtls library used in the SPE. Trusted Firmware-M also has the ability to perform symmetric crypto and key negotiation but this functionality is not currently used.

## 3 Project Configuration
### 3.1 Flash Protection Mechanisms
The internal NOR flash memory on the STM32U5 is separated into two bank of 1024 KB for a total of 2MB of NOR flash. Each bank is further separated into 128 pages of 8 KB.

Programming Operations require a minimum block size of 4 32-bit Words or 16 Bytes.

Erase Operations require a minimum size of 1 page = 8 KB.

#### SECWM: Secure Watermark
When TrustZone is enabled (with the TZEN=1 option bit), Secure Watermark flash area protection can be enabled. This restricts read and write access to code running when the processor is in the *Secure* state. The STM32U5 has two Secure Watermark regions defined by the SECWM1_PSTRT, SECWM1_PEND, SECWM2_PSTRT, and SECWM2_PEND option bits.

The contents of SECWMx_PSTRT defines the starting page (8K) of the SECWM area for flash bank x, while SECWMx_PEND defines the ending page of the SECWM area for bank x. Both SECWMx_PSTRT and SECWMx_PEND are 7 bits wide.

Setting the HDPx_ACCDIS option bit will result in the SECWM configuration being locked until the next reset occurs.

#### HDP: Secure Hide Protection
When a valid SECWM region is defined, Secure Hide Protection (HDP) can be enabled for part or all of a given SECWM region. HDP is typically enabled at runtime via the HDPx_ACCDIS bit in the FLASH_SECHDPCR register to protect bootloader code and data from modification after the bootloader has completed running.

The beginning page number of an HDP area is defined by the corresponding SECWMx_PSTRT option bits while the ending page is defined by the HDPx_PEND option bits.

The HDPxEN option bit is used to enable or disable the specified HDP region in a persistent way. The HDPx_ACCDIS is always cleared to 0 after a reset. Both the HDPxEN option bit and HDPx_ACCDIS register bit must be true for Secure Hide Protection to be enabled.

#### Write Protection (WRP)
Each flash bank may have up to two write-protected regions referred to as regions A and B. Given that the STM32U5 has two flash banks, there are four total WRP regions available: WRP1A, WRP1B, WRP2A, WRP2B. Similar to the SECWM feature, Write Protection regions are defined by a starting and ending page.

The WRPxy_PSTRT option bits defines the starting page number of a WRP region, while the WRPxy_PEND option bits define the ending page number of a given region.

The UNLOCK bit in the FLASH_WRPxyR option byte register can be cleared to 0 to write protect an area of flash until an RDP regression from Level 1 to Level 0 occurs. This mechanism is used to protect most of of the BL2 bootloader.

#### Readout Protection (RDP)
The ReDdout Protection level is the way in which the microcontroller may be *locked* to a particular configuration. In addition, the RDP level determines whether or not access to S / NS memory is possible via

| RDP Level | Description                   | SPE Debug | NSPE Debug    | Option Bytes |
|-----------|-------------------------------|-----------|---------------|--------------|
| 0         | Open / Unlocked               | Enabled   | Enabled       | Modifiable   |
| 0.5       | NSPE Debug only               | Blocked   | Enabled       | Modifiable   |
| 1         | SPE / NSPE memory protected   | Blocked   | Blocked*      | Modifiable   |
| 2         | Locked                        | Blocked   | Blocked       | Read-Only*   |

If an OEM1 or OEM2 key is provisioned prior to entering RDP Level 2, regressing back to level 0 is possible with a mass erase of all of the STM32U5 internal flash.

#### Flash Privilege Protection
The FLASH_PRIVCFGR register contains two bits NSPRIV and SPRIV which determine whether flash registers may be accessed by code running in non-privileged mode. Privilege level is separate from the TrustZone Secure / Non-Secure state.  When the SPRIV or NSPRIV bits are set, access to the S or NS flash registers requires that the MCU be in privileged mode (determined by nPRIV in the CONTROL register).

FLASH_PRIVCFGR is not used by this demo.

#### Block based Flash protection
Block based flash protection is organized into 4 32 bit registers per flash bank. This means that each bit controls access to a single 8 KB flash page.

The FLASH_PRIVBB1Rx and FLASH_PRIVBB2Rx registers control the privilege level required to access a given page of flash.

Similarly, the FLASH_SECBB1Rx and FLASH_SECBB2Rx registers control the Security state of the processor required to access a given page of flash.

Both Security Block Based protection and Privlege level Block Based protection must be configured at runtime.

### 3.2 Flash Memory Layout
The following table summarizes the flash layout used in this project:

| Offset     | Bank |  Pages  | Region Name                     | Size (Dec.) | Size (Hex.) | Image Suffix |
|------------|------|---------|---------------------------------|-------------|-------------|------------  |
| 0x00000000 | 1    | 0, 7    | Scratch                         |    64 KB    |   0x10000   | N/A          |
| 0x00010000 | 1    | 8, 8    | BL2 - NV Counters               |     8 KB    |   0x02000   |              |
| 0x00012000 | 1    | 9, 9    | BL2 - NV Counters initial value |     8 KB    |   0x02000   | _bl2.bin     |
| 0x00014000 | 1    | 10, 21  | BL2 - MCUBoot HDP Code          |    96 KB    |   0x18000   | _bl2.bin     |
| 0x0002C000 | 1    | 22, 25  | BL2 - SPE Shared Code           |    28 KB    |   0x07000   | _bl2.bin     |
| 0x00033000 | 1    | 25, 25  | OTP Write Protect               |     4 KB    |   0x01000   | N/A          |
| 0x00034000 | 1    | 26, 27  | NV counters area                |    16 KB    |   0x04000   | N/A          |
| 0x00038000 | 1    | 28, 29  | Secure Storage Area             |    16 KB    |   0x04000   | N/A          |
| 0x0003C000 | 1    | 30, 31  | Internal Trusted Storage Area   |    16 KB    |   0x04000   | N/A          |
| 0x00040000 | 1    | 32, 63  | Secure image     primary slot   |   256 KB    |   0x40000   | _s_signed    |
| 0x00080000 | 1-2  | 64, 16  | Non-secure image primary slot   |   640 KB    |   0xA0000   | _ns_signed   |
| 0x00120000 | 2    | 17, 48  | Secure image     secondary slot |   256 KB    |   0x40000   | s_update     |
| 0x00160000 | 2    | 49, 127 | Non-secure image secondary slot |   640 KB    |   0xA0000   | ns_update    |

### 3.3 Flash Option Byte Configuration
The STM32U5 microcontroller uses option bytes to enable and configure various hardware features, particularly security related features.

Refer to the ST RM0456 document for a more exhaustive description of each option byte.

## 4 Building the Firmware Images
After importing the b_u585i_iot02a_tfm project into STM32CubeIDE, Build the project by highlighting it in the *Project Explorer* pane and then clicking **Project -> Build Project** from the menu at the top of STM32CubeIDE.

## 5 Customizing the Firmware Metadata for mcuboot
### 5.1 Using custom firmware region Signing Keys
Run the following openssl commands to generate a new set of RSA 3072 bit signing keys:
```
imgtool keygen -t rsa-3072 -k spe_signing_key.pem
imgtool keygen -t rsa-3072 -k nspe_signing_key.pem

```

Then modify the S_REGION_SIGNING_KEY and NS_REGION_SIGNING_KEY variables in the [project_defs.mk](project_defs.mk) file so that they contain the absolute paths to the relevant region signing keys.

## 5.2 Setting the version number and Anti-Rollback counter for each image
Modify the SPE_VERSION and NSPE_VERSION variables in [project_defs.mk](project_defs.mk) to set the version number encoded in the image header.

Similarly, you can modify the anti rollback counter for each image by setting the SPE_SECURITY_COUNTER and NSPE_SECURITY_COUNTER variables.


## 6 Build Artifacts

The b_u585i_iot02a_tfm project generates quite a few image files, each with it's own set of use cases. The table below explains the use of each image generated by this project.

| Name | Contents | Mcuboot Metadata | Mcuboot “confirmed” | Target / load address offset           | Image Size        | Region size      | Protection Method                                                                          | When to use this image                   |   |
|------------------------------|----------------------------------------------------------|------------------|---------------------|----------------------------------------|-------------------|------------------|--------------------------------------------------------------------------------------------|------------------------------------------|---|
| b_u585i_iot02a_tfm_bl2       | BL2 Bootloader                                           | No               | N/A                 | FLASH_AREA_BL2_BIN_OFFSET (0x00012000) | ~120 KB (0x1E000) | 132 KB (0x21000) | Stored in SECWM / HDP / WRP protected flash. | When debugging the BL2 bootloader.       |   |
| b_u585i_iot02a_tfm_s         | SPE firmware region (TF-M)                               | No               | N/A                 | FLASH_AREA_0_OFFSET + BL2_HEADER_SIZE  | ~246 KB (0x3D800) | 256 KB (0x40000) | N/A                                                                                        | Intermediate build artifact. Don’t use.  |   |
| b_u585i_iot02a_tfm_ns        | NSPE firmware region (FreeRTOS, etc)                     | No               | N/A                 | FLASH_AREA_1_OFFSET + BL2_HEADER_SIZE  | ~609 KB (0x98400) | 640 KB (0xA0000) | N/A                                                                                        | Intermediate build artifact. Don’t use   |   |
| b_u585i_iot02a_tfm_s_signed  | b_u585i_iot02a_tfm_s w/mcuboot info                      | Yes              | Yes                 | FLASH_AREA_0_OFFSET                    | 256 KB (0x40000)  | 256 KB (0x40000) | Mcuboot metadata verification.                                                             | Write to SPE region during debugging.    |   |
| b_u585i_iot02a_tfm_s_ota     | b_u585i_iot02a_tfm_s w/mcuboot info                      | Yes              | No                  | FLASH_AREA_2_OFFSET                    |                   |                  | Mcuboot metadata verification.                                                             | SPE update via FreeRTOS OTA              |   |
| b_u585i_iot02a_tfm_ns_signed | b_u585i_iot02a_tfm_ns w/mcuboot info                     | Yes              | Yes                 | FLASH_AREA_1_OFFSET                    | 640 KB (0xA0000)  | 640 KB (0xA0000) | Mcuboot metadata verification.                                                             | Write to NSPE region during debugging.   |   |
| b_u585i_iot02a_tfm_ns_ota    | b_u585i_iot02a_tfm_ns w/mcuboot info                     | Yes              | No                  | FLASH_AREA_3_OFFSET                    |                   |                  | Mcuboot metadata verification.                                                             | NSPE update via FreeRTOS OTA             |   |
| b_u585i_iot02a_tfm_s_update  | b_u585i_iot02a_tfm_s_signed with modified load address.  | Yes              | Yes                 | FLASH_AREA_2_OFFSET                    | 256 KB (0x40000)  | 256 KB (0x40000) | Mcuboot metadata verification.                                                             | SPE local upgrade via jtag.              |   |
| b_u585i_iot02a_tfm_ns_update | b_u585i_iot02a_tfm_ns_signed with modified load address. | Yes              | Yes                 | FLASH_AREA_3_OFFSET                    | 640 KB (0xA0000)  | 640 KB (0xA0000) | Mcuboot metadata verification.                                                             | NSPE local upgrade via jtag.             |   |
b_u585i_iot02a_tfm_bl2_s_ns_factory | b_u585i_iot02a_tfm_bl2, b_u585i_iot02a_tfm_s_signed, b_u585i_iot02a_tfm_ns_signed | Yes | Yes | FLASH_AREA_BL2_BIN_OFFSET, FLASH_AREA_1_OFFSET, FLASH_AREA_2_OFFSET | 1028 KB (0x101000) | 1028 KB (0x101000) | SECWM/HDP/WRP immutability (for BL2), Mcuboot metadata verification (for SPE/NSPE) | Initial “factory” firmware installation via jtag |

## 7 Flashing the Image from STM32CubeIDE
In STM32CubeIDE, the following "External Tools" targets are available to aid in configuring the board with and without trustzone enabled.

| External Tool Name | stm32u5_tool.sh cli argument | Description |
|--------------------|------------------------------|-------------|
| Flash_Unlock       | unlock                       | Remove Secure Watermark and Write Protection to enable a new BL2 image to be deployed. |
| Trustzone_Disable  | tz_regression                | Clear the TZEN bit and erase all of the internal flash to allow running the b_u585i_iot02a_ntz application. |
| Trustzone_Enable   | tz_enable                    | Set the TZEN bit and enable flash protection features to allow running the b_u585i_iot02a_tfm application. |

In addition to the **External Tools**, a few **Run Configurations** are also defined:

| Run Configuration Name    | stm32u5_tool.sh argument | Target Image (s)                          |
|---------------------------|--------------------------|-------------------------------------------|
| Flash_tfm_bl2_s_ns        | flash_tzen_all           | b_u585i_iot02a_tfm_bl2_s_ns_factory.hex   |
| N/A                       | flash_s                  | b_u585i_iot02a_tfm_s_signed.hex           |
| Flash_tfm_ns              | flash_ns                 | b_u585i_iot02a_tfm_ns_signed.hex          |
| N/A                       | flash_s_ns               | b_u585i_iot02a_tfm_s_ns_signed.hex        |
| Flash_tfm_ns_update       | flash_ns_update          | b_u585i_iot02a_tfm_ns_update.hex      |
| Flash_tfm_s_ns_update     | flash_tzen_update        | b_u585i_iot02a_tfm_s_ns_update.hex    |
| b_u585i_iot02a_tfm_debug  | N/A                      | b_u585i_iot02a_tfm_s_ns_update.hex (flashing step),<br />b_u585i_iot02a_tfm_s.elf (debug symbols),<br />b_u585i_iot02a_tfm_ns.elf (debug symbols) |

After running the "TrustZone Enable" target, it is recommended to use the **Flash_tfm_bl2_s_ns** Run Configuration to flash the first BL2 (Bootloader), SPE, and NSPE regions all at once.

Subsequent updates may use either the s_update, ns_update, s_ns_update, s_signed, ns_signed, or s_ns_signed targets as necessary.

When building an image for use with the FreeRTOS OTA service, please use the b_u585i_iot02a_tfm_ns_ota.bin and b_u585i_iot02a_tfm_s_ota.bin images.

## 8 Flashing the Image from the command line

Check that the STM32Programmer_CLI binary is in your PATH and run the following:
```
# Add STM32Programmer_CLI bin directory to the shell PATH (Mac)
export PATH=/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.macos64_2.0.200.202202231230/tools/bin:$PATH

# Add tools directory to your PATH
source tools/env_setup.sh

# Change to the project directory
cd Projects/b_u585i_iot02a_tfm

# Run the flashing script.
stm32u5_tool.sh flash_tzen_all
```

## 9 Performing Over-the-air (OTA) Firmware Update

The project shows an IoT reference implementation of how to integrate FreeRTOS libraries on STM32U5 platform to perform OTA update with AWS IoT using the TF-M implementation of the PSA Firmware Update API.

The demo runs FreeRTOS OTA agent as one of the RTOS tasks in background, which waits for OTA updates from cloud.

### 9.1 Specificities compared to the description available in the [Getting Started Guide](../../Getting_Started_Guide.md):

- Multiple images

An OTA job may update either the non-secure image partition, or the secure image partition.

The image to update must be specified in the `files[].fileName` field of the `ota-update-job-config.json` file: `"non_secure image"`, or `"secure image"` for the OTA Agent to identify the image properly. Otherwise no image is downloaded.

The `files[].fileLocation.s3Location.key` field should then be set to `b_u585i_iot02a_tfm_ns_ota.bin` or `b_u585i_iot02a_tfm_s_ota.bin`,
respectively.

- Image versions

The image state is managed by the secure bootloader and by the firmware update secure service, so the user-side OTA PAL is not used (no `/ota_pal/ota_firmware_version.c` where to modify `APP_VERSION_MAJOR`).

Instead, the fields `NSPE_VERSION` and `NSPE_SECURITY_COUNTER` (respectively `SPE_VERSION` and `SPE_SECURITY_COUNTER` for the *secure image*)
must be updated in the` project_defs.mk` file so that the firmware update version could be retrieved from the image header, and validated.
Otherwise the image self-test fails after install, the update is reverted, and the OTA job is reported as failed.

## 10 Performing Integration Test

Integration test is run when any of the execution parameter is enabled in [test_execution_config.h](../../Common/config/test_execution_config.h).

### 10.1 Prerequisite

- Run [OTA](#9-performing-over-the-air-ota-firmware-update) once manually.
- Set [TEST_AUTOMATION_INTEGRATION](../../Common/config/ota_config.h) to 1.

### 10.2 Steps for each test case

1. Device Advisor Test
    - Set DEVICE_ADVISOR_TEST_ENABLED to 1 in [test_execution_config.h](../../Common/config/test_execution_config.h).
    - Create a [device advisor test](https://docs.aws.amazon.com/iot/latest/developerguide/device-advisor.html) on website. ( Iot Console -> Test -> Device Advisor )
    - Create test suite.
    - Run test suite and set the device advisor endpoint to MQTT_SERVER_ENDPOINT in [test_param_config.h](../../Common/config/test_param_config.h).
    - Set MQTT_SERVER_PORT and IOT_THING_NAME (Same as provisioned one) in [test_param_config.h](../../Common/config/test_param_config.h).
    - Build and run with command below.
        ```
        stm32u5_tool.sh flash_tzen_update
        ```
    - See device advisor test result on website.
1. MQTT Test
    - Set MQTT_TEST_ENABLED to 1 in [test_execution_config.h](../../Common/config/test_execution_config.h).
    - Set the MQTT endpoint to MQTT_SERVER_ENDPOINT in [test_param_config.h](../../Common/config/test_param_config.h).
    - Set MQTT_SERVER_PORT and IOT_THING_NAME (Same as provisioned one) in [test_param_config.h](../../Common/config/test_param_config.h).
    - Build and run with command below.
        ```
        stm32u5_tool.sh flash_tzen_update
        ```
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
    - Build and run with command below.
        ```
        stm32u5_tool.sh flash_tzen_update
        ```
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
    - Set OTA_PAL_FIRMWARE_FILE to "non_secure image" in [test_param_config.h](../../Common/config/test_param_config.h).
    - Build and run with command below.
        ```
        stm32u5_tool.sh flash_tzen_update
        ```
    - See test result on target output.
    - Note: Test case "otaPal_CloseFile_ValidSignature" fail because TFM doesn't support dummy file name.
    - Example output
        ```
        <INF>    14604 [QualTest  ] ---------STARTING TESTS--------- (qualification_app_main.c:102)
        <INF>    15604 [QualTest  ] TEST(Full_OTA_PAL, otaPal_CloseFile_ValidSignature)C:/git/ActoryForked/iot-reference-stm32u5/Middleware/FreeRTOS/FreeRTOS-Libraries-Integration-Tests/src/ota/ota_pal_test.c:142::FAIL: Expected 0 Was 228 (qualification_app_main.c:102)
        ...
        <INF>    33223 [QualTest  ]  (qualification_app_main.c:102)
        <INF>    34223 [QualTest  ] ----------------------- (qualification_app_main.c:102)
        <INF>    35223 [QualTest  ] 15 Tests 1 Failures 0 Ignored  (qualification_app_main.c:102)
        <INF>    36223 [QualTest  ] FAIL (qualification_app_main.c:102)
        <INF>    37223 [QualTest  ] -------ALL TESTS FINISHED------- (qualification_app_main.c:102)
        ```
1. Core PKCS11 Test
    - TFM doesn't have corePKCS11, skip it.

## 11 Run AWS IoT Device Tester

This repository can be tested using [AWS IoT Device Tester for FreeRTOS (IDT)](https://aws.amazon.com/freertos/device-tester/). IDT is a downloadable tool that can be used to exercise a device integration with FreeRTOS to validate functionality and compatibility with Amazon IoT cloud. Passing the test suite provided by IDT is also required to qualify a device for the [Amazon Partner Device Catalogue](https://devices.amazonaws.com/).

IDT runs a suite of tests that include testing the device's transport interface layer implementation, PKCS11 functionality, and OTA capabilities. In IDT test cases, the IDT binary will make a copy of the source code, update the header files in the project, then compile the project and flash the resulting image to your board. Finally, IDT will read serial output from the board and communicate with the AWS IoT cloud to ensure that test cases are passing.

### 11.1 Prerequisite
- Run [OTA](#5-performing-over-the-air-ota-firmware-update) once manually.
- Set [TEST_AUTOMATION_INTEGRATION](../../Common/config/ota_config.h) to 1.

### 11.2 Download AWS IoT Device Tester

The latest version of IDT can be downloaded from the [public documentation page](https://docs.aws.amazon.com/freertos/latest/userguide/dev-test-versions-afr.html). This repository only supports test suites FRQ_2.2.0 or later.

### 11.3 Configure AWS IoT Device Tester

*In `devicetester_freertos_win/tests/FRQ_x.y.z/suite`, please extend the timeout value to 9000000 in `test.json` for test cases `full_cloud_iot`, `full_pkcs11_core`, `full_pkcs11_preprovisioned_ecc`, `ota_core`, `ota_dataplane_mqtt`, and `transportInterfaceTLS`.*

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

* In `build.bat` / `build.sh`, update ST_IDE_PATH, and TOOLCHAIN_PATH
* In `flash.bat` / `flash.sh`, update TOOLCHAIN_PATH, and BASH_EXE

* In `config.json`, update the `profile` and `awsRegion` fields
* In `device.json`, update `serialPort` to the serial port of your board. Update `publicKeyAsciiHexFilePath` to the absolute path to `dummyPublicKeyAsciiHex.txt`. Update `publicDeviceCertificateArn` to the ARN of the certificate uploaded when [Setup AWS IoT Core](./GettingStartedGuide.md#21-setup-aws-iot-core).
* In `userdata.json`, update `sourcePath` to the absolute path to the root of this repository.
* In `userdata.json`, update `signerCertificate` with the ARN of the [Setup pre-requisites for OTA cloud resources
.](./GettingStartedGuide.md#51-setup-pre-requisites-for-ota-cloud-resources)
* Run all the steps to create a [second code signing certificate](./GettingStartedGuide.md#51-setup-pre-requisites-for-ota-cloud-resources) but do NOT provision the key onto your board. Copy the ARN for this certificate in `userdata.json` for the field `untrustedSignerCertificate`.

### 11.4 Running AWS IoT Device Tester

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
.\devicetester_win_x86-64.exe run-suite --skip-group-id FullPKCS11_PreProvisioned_ECC --skip-group-id FullPKCS11_Core --skip-group-id FullTransportInterfacePlainText
```

For more information, `.\devicetester_win_x86-64.exe help` will show all available commands.

When IDT is run, it generates the `results/uuid` directory that contains the logs and other information associated with your test run, allowing failures to easily be debugged.
