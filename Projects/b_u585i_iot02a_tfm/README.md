# TrustZone / TFM Enabled Demo Project
The b_u585i_iot02a_tfm project utilizes [ARM Trusted Firmware M](https://www.trustedfirmware.org/projects/tf-m/) as the TrustZone Secure Processing Environment firmware and bootloader.

In this project, Trusted Firmware-M is configured to use the [ Large Profile ](https://tf-m-user-guide.trustedfirmware.org/docs/technical_references/design_docs/profiles/tfm_profile_large.html) with [ Isolation Level 2 ](https://tf-m-user-guide.trustedfirmware.org/docs/technical_references/design_docs/ff_isolation.html). Isolation Level 3 support is likely to be implemented for the STM32U5 MCU at some point in the future.

TF-M utilizes the TrustZone features of the Cortex-M33 core to provide isolation between code running in the NSPE (Non Secure Processing Environment) amd code running in the Secure Processing Environment (SPE).
In addition, the secure region MPU is used to provide isolation between different portions of the SPE (Secure Processing Environment) firmware.

## Definitions
**NSPE**: Non Secure Processing Environment

**SPE**: Secure Processing Environment

**Secure Boot**: The combination of signed application images, an immutable public key(s), and an immutable bootloader which verifies the signature appended to each application image against the relevant public key(s) prior to installation and execution.

**PSA**: ARM's Platform Security Architecture

**PSA APIs**: ARM's [PSA Crypto](https://armmbed.github.io/mbed-crypto/html/), [FWU (Firmware Update)](https://developer.arm.com/documentation/ihi0093/0000/),

**PSA Root of Trust (PRoT)**: The security sensitive portions of the SPE firmware provided by Trusted Firmware M.

**Application Root of Trust (ARoT)**: The remaining portions of the SPE firmware provided by Trusted Firmware M and the application developer.

## Features

### Secure Boot / Secure Firmware Update
In this project, TF-M is primarily used primarily for secure boot, firmware update functionality, and private key operations (key generation and signing). The [mcuboot](https://www.mcuboot.com/) library forms a core part of the BL2 bootloader component of TF-M and forms the basis of the secure boot and firmware update implementation. Mcuboot defines the metadata format used for the header and trailer of each firmware image. This metadata include a digest (hash) of the firmware, cryptographic signature, version number, list of dependencies on other modules, and a variety of other flags. The BL2 bootloader verifies each image prior to every boot.

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

### FreeRTOS OTA Platform Abstraction Layer implementation

The OTA Platform Abstraction Layer implementation is primarily defined by

# Project Configuration

## Flash Protection Mechanisms
### Flash Memory Layout
The internal NOR flash memory on the STM32U5 is separated into two bank of 1024 KB for a total of 2MB of NOR flash. Each bank is further separated into 128 pages of 8 KB.

Programming Operations require a minimum block size of 4 32-bit Words or 16 Bytes.

Erase Operations require a minimum size of 1 page = 8 KB.
### SECWM: Secure Watermark
When TrustZone is enabled (with the TZEN=1 option bit), Secure Watermark flash area protection can be enabled. This restricts read and write access to code running when the processor is in the *Secure* state. The STM32U5 has two Secure Watermark regions defined by the SECWM1_PSTRT, SECWM1_PEND, SECWM2_PSTRT, and SECWM2_PEND option bits.

The contents of SECWMx_PSTRT defines the starting page (8K) of the SECWM area for flash bank x, while SECWMx_PEND defines the ending page of the SECWM area for bank x. Both SECWMx_PSTRT and SECWMx_PEND are 7 bits wide.

Setting the HDPx_ACCDIS option bit will result in the SECWM configuration being locked until the next reset occurs.

### HDP: Secure Hide Protection
When a valid SECWM region is defined, Secure Hide Protection (HDP) can be enabled for part or all of a given SECWM region. HDP is typically enabled at runtime via the HDPx_ACCDIS bit in the FLASH_SECHDPCR register to protect bootloader code and data from modification after the bootloader has completed running.

The beginning page number of an HDP area is defined by the corresponding SECWMx_PSTRT option bits while the ending page is defined by the HDPx_PEND option bits.

The HDPxEN option bit is used to enable or disable the specified HDP region in a persistent way. The HDPx_ACCDIS is always cleared to 0 after a reset. Both the HDPxEN option bit and HDPx_ACCDIS register bit must be true for Secure Hide Protection to be enabled.

### Write Protection (WRP)
Each flash bank may have up to two write-protected regions referred to as regions A and B. Given that the STM32U5 has two flash banks, there are four total WRP regions available: WRP1A, WRP1B, WRP2A, WRP2B. Similar to the SECWM feature, Write Protection regions are defined by a starting and ending page.

The WRPxy_PSTRT option bits defines the starting page number of a WRP region, while the WRPxy_PEND option bits define the ending page number of a given region.

The UNLOCK bit in the FLASH_WRPxyR option byte register can be cleared to 0 to write protect an area of flash until an RDP regression from Level 1 to Level 0 occurs. This mechanism is used to protect most of of the BL2 bootloader.

### Readout Protection (RDP)
The ReDdout Protection level is the way in which the microcontroller may be *locked* to a particular configuration. In addition, the RDP level determines whether or not access to S / NS memory is possible via

| RDP Level | Description                   | SPE Debug | NSPE Debug    | Option Bytes |
|-----------|-------------------------------|-----------|---------------|--------------|
| 0         | Open / Unlocked               | Enabled   | Enabled       | Modifiable   |
| 0.5       | NSPE Debug only               | Blocked   | Enabled       | Modifiable   |
| 1         | SPE / NSPE memory protected   | Blocked   | Blocked*      | Modifiable   |
| 2         | Locked                        | Blocked   | Blocked       | Read-Only*   |

If an OEM1 or OEM2 key is provisioned prior to entering RDP Level 2, regressing back to level 0 is possible with a mass erase of all of the STM32U5 internal flash.

### Flash Privilege Protection
The FLASH_PRIVCFGR register contains two bits NSPRIV and SPRIV which determine whether flash registers may be accessed by code running in non-privileged mode. Privilege level is separate from the TrustZone Secure / Non-Secure state.  When the SPRIV or NSPRIV bits are set, access to the S or NS flash registers requires that the MCU be in privileged mode (determined by nPRIV in the CONTROL register).

FLASH_PRIVCFGR is not used by this demo.

### Block based Flash protection
Block based flash protection is organized into 4 32 bit registers per flash bank. This means that each bit controls access to a single 8 KB flash page.

The FLASH_PRIVBB1Rx and FLASH_PRIVBB2Rx registers control the privilege level required to access a given page of flash.

Similarly, the FLASH_SECBB1Rx and FLASH_SECBB2Rx registers control the Security state of the processor required to access a given page of flash.

Both Security Block Based protection and Privlege level Block Based protection must be configured at runtime.


## Flash Memory Layout
The following table summarizes the flash layout used in this project:

| Offset     | Bank |  Pages  | Region Name                     | Size (Dec.) | Size (Hex.) | Image Suffix | Flash Protection |
|------------|------|---------|---------------------------------|-------------|-------------|------------  | -----------------|
| 0x00000000 | 1    | 0, 7    | Scratch                         |    64 KB    |   0x10000   | N/A          | SECWM            |
| 0x00010000 | 1    | 8, 8    | BL2 - NV Counters               |     8 KB    |   0x02000   |              |                  |
| 0x00012000 | 1    | 9, 9    | BL2 - NV Counters inital value  |     8 KB    |   0x02000   | _bl2.bin     | SECWM, HDP, WRP  |
| 0x00014000 | 1    | 10, 21  | BL2 - MCUBoot HDP Code          |    96 KB    |   0x18000   | _bl2.bin     | SECWM, HDP, WRP  |
| 0x0002C000 | 1    | 22, 25  | BL2 - SPE Shared Code           |    28 KB    |   0x07000   | _bl2.bin     |
| 0x00033000 | 1    | 25, 25  | OTP Write Protect               |     4 KB    |   0x01000   |              |
| 0x00034000 | 1    | 26, 27  | NV counters area                |    16 KB    |   0x04000   |              |
| 0x00038000 | 1    | 28, 29  | Secure Storage Area             |    16 KB    |   0x04000   |              |
| 0x0003C000 | 1    | 30, 31  | Internal Trusted Storage Area   |    16 KB    |   0x04000   |              |
| 0x00040000 | 1    | 32, 63  | Secure image     primary slot   |   256 KB    |   0x40000   |              |
| 0x00080000 | 1-2  | 64, 16  | Non-secure image primary slot   |   640 KB    |   0xA0000   |              |
| 0x00120000 | 2    | 17, 48  | Secure image     secondary slot |   256 KB    |   0x40000   |              |
| 0x00160000 | 2    | 49, 127 | Non-secure image secondary slot |   640 KB    |   0xA0000   |              |

## Flash Option Byte Configuration
The STM32U5 microcontroller uses option bytes to enable and configure various hardware features, particularly security related features.

Refer to the ST RM0456 document for a more exhaustive description of each option byte.


### TrustZone Enable (TZEN)
The TZEN option byte controls whether or not TrustZone is enabled on the STM32U5 MCU.
Refer to
### Secure Watermark (SECWM)
### Secure Hide Protection (HDP)
### Secure Boot Address (SECBOOTADD0)
### Flash Write Protection (WRP)

## Building the Firmware Images

After importing the b_u585i_iot02a_ntz project into STM32CubeIDE, Build the project by highlighting it in the *Project Explorer* pane and then clicking **Project -> Build Project** from the menu at the top of STM32CubeIDE.

## Customizing the Firmware Metadata for mcuboot
### Using custom firmware region Signing Keys
Run the following openssl commands to generate a new set of RSA 3072 bit signing keys:
```
imgtool keygen -t rsa-3072 -k spe_signing_key.pem
imgtool keygen -t rsa-3072 -k nspe_signing_key.pem

```

Then modify the S_REGION_SIGNING_KEY and NS_REGION_SIGNING_KEY variables in the [project_defs.mk](project_defs.mk) file so that they contain the absolute paths to the relevant region signing keys.

## Setting the version number and Anti-Rollback counter for each image
Modify the SPE_VERSION and NSPE_VERSION variables in [project_defs.mk](project_defs.mk) to set the version number encoded in the image header.

Similarly, you can modify the anti rollback counter for each image by setting the SPE_SECURITY_COUNTER and NSPE_SECURITY_COUNTER variables.


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

## Flashing the Image from the command line

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
