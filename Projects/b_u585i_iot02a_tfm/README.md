# TrustZone / TFM Enabled Demo Project
The b_u585i_iot02a_tfm project utilizes [ARM Trusted Firmware M](https://www.trustedfirmware.org/projects/tf-m/) as the TrustZone Secure Processing Environment firmware and bootloader.

In this project, Trusted Firmware-M is configured to use the [ Large Profile ](https://tf-m-user-guide.trustedfirmware.org/docs/technical_references/design_docs/profiles/tfm_profile_large.html) with [ Isolation Level 2 ](https://tf-m-user-guide.trustedfirmware.org/docs/technical_references/design_docs/ff_isolation.html). Isolation Level 3 support is likely to be implemented for the STM32U5 MCU at some point in the future.

TF-M utilizes the TrustZone features of the Cortex-M33 core to provide isolation between code running in the NSPE (Non Secure Processing Environment) amd code running in the Secure Processing Environment (SPE). In addition, the secure region MPU is used to provide isolation between different portions of the SPE (Secure Processing Environment) firmware.

## Features

### Secure Boot / Secure Firmware Update
In this project, TF-M is primarily used primarily for secure boot, firmware update functionality, and private key operations (key generation and signing). The [ mcuboot ](https://www.mcuboot.com/) library forms a core part of the BL2 bootloader component of TF-M and forms the basis of the secure boot and firmware update implementation. Mcuboot defines the metadata format used for the header and trailer of each firmware image. This metadata include a digest (hash) of the firmware, cryptographic signature, version number, list of dependencies on other modules, and a variety of other flags. The BL2 bootloader verifies each image prior to every boot.

### Anti-Rollback
Anti Rollback is achieved using the MCUboot and TF-M "Security Counter" Implementation detailed on the https://tf-m-user-guide.trustedfirmware.org/docs/technical_references/design_docs/tfm_secure_boot.html page.
For each region (SPE and NSPE), updates must have a security counter value which is greater than or equal to the current security counter for that region.

During an OTA update, an invalid image security counter will result in the update image will be rejected and the BL2 will revert to the previous image for that region. After an OTA update is booted and "confirmed" via a self-test, the corresponding security counter is incremented to prevent rollback during the next reset cycle. The FreeRTOS OTA implementation included in this project performs this reset automatically.

If a region is updated locally via JTAG (or another method which includes an image with the "confirmed" flag pre-set) to an image with a security counter less than the current security counter for that region, booting will fail.

### Cryptographic Operations
Private Key operations are performed via calls to the [ ARM PSA Cryptography API ](https://armmbed.github.io/mbed-crypto/html). Functions that transition the microcontroller from NS mode to S mode (veneer functions) are tightly controlled and located in a specific region of program flash which is marked "Non Secure Callable". Generic NSPE code cannot cause a switch to S mode, but Non-Secure Callable veener functions may. Keys are stored in the TFM "Internal Trusted Storage" flash region, which is only accessible by the TFM Root of Trust region (isolation level 2) or TFM crypto service (isolation level 3).

Bulk symmetric crypto, signature verification, and key negotiation operations occur in the NSPE using the same version of the mbedtls library used in the SPE. Trusted Firmware-M also has the ability to perform symmetric crypto and key negotiation but this functionality is not currently used.

## Software Components

### ARM Trusted Firmware M

### BL2 Bootloader / mcuboot

### FreeRTOS OTA Server Platform Abstraction Layer implementation

## Term Definitions
**NSPE**: Non Secure Processing Environment

**SPE**: Secure Processing Environment

**Secure Boot**: The combination of signed application images, an immutable public key(s), and an immutable bootloader which verifies the signature appended to each application image against the immutable private key(s) prior to installation and execution.

**PSA**: ARM's Platform Security Architecture

**PSA APIs**: A set of High-Level APIs which provide cryptographic, storage, firmware update, attestation, and other security related services.

**PSA Root of Trust (PRoT)**: The security sensitive portions of the SPE firmware provided by Trusted Firmware M.

**Application Root of Trust (ARoT)**: The remaining portions of the SPE firmware provided by Trusted Firmware M and the application developer.

# Project Configuration

## Flash Memory Layout
The following table summarizes the flash layout used in this project:

| Offset     | Region Name                     | Size (Dec.) | Size (Hex.) |
|------------|---------------------------------|-------------|-------------|
| 0x00000000 | Scratch                         |    64 KB    |   0x10000   |
| 0x00010000 | BL2 - counters                  |    16 KB    |   0x04000   |
| 0x00014000 | BL2 - MCUBoot                   |   124 KB    |   0x1F000   |
| 0x00033000 | OTP Write Protect               |     4 KB    |   0x01000   |
| 0x0002E000 | NV counters area                |    16 KB    |   0x04000   |
| 0x00032000 | Secure Storage Area             |    16 KB    |   0x04000   |
| 0x00036000 | Internal Trusted Storage Area   |    16 KB    |   0x04000   |
| 0x0003A000 | Secure image     primary slot   |   256 KB    |   0x40000   |
| 0x0007A000 | Non-secure image primary slot   |   640 KB    |   0xA0000   |
| 0x0011A000 | Secure image     secondary slot |   256 KB    |   0x40000   |
| 0x0015A000 | Non-secure image secondary slot |   640 KB    |   0xA0000   |

## Flash Opotion Byte Configuration
### TrustZone Enable (TZEN)
### Readout Protection (RDP)
### Secure Watermark (SECWM)
### Secure Hide Protection (HDP)
### Secure Boot Address (SECBOOTADD0)
### Flash Write Protection (WRP)

## Building the Firmware Images

After importing the b_u585i_iot02a_ntz project into STM32CubeIDE, Build the project by highlighting it in the *Project Explorer* pane and then clicking **Project -> Build Project** from the menu at the top of STM32CubeIDE.

## Customizing the Firmware Signing Keys
### Generate S and NS signing keys with openssl
Run the following openssl commands to generate a new set of RSA 3072 bit signing keys:
```
openssl genrsa -out spe_signing_key.pem 3072
openssl genrsa -out nspe_signing_key.pem 3072
```

Then modify the [tfm.mk](tfm.mk) file and set the S_REGION_SIGNING_KEY and NS_REGION_SIGNING_KEY to the absolute paths to your newly generated signing keys.

## Setting the version number and Anti-Rollback counter for each image


## Build Artifacts

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

## Flashing the Image from STM32CubeIDE
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

## Flashing the Image from the commandline

Check that the STM32Programmer_CLI binary is in your PATH and run the following:
```
# Add STM32Programmer_CLI bin directory to the shell PATH (Mac)
export PATH=/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.macos64_2.0.200.202202231230/tools/bin:$PATH

# Add tools directory to your PATH
source tools/env_setup.sh

# Change to the project directory
cd Projects/b_u585i_iot02a_tfm

# Run the flashing script.
stn32u5_tool.sh flash_tzen_all
```

## Performing Over-the-air (OTA) Firmware Update
