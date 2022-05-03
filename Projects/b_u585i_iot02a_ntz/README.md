# Non-TrustZone Demo Project
The following Readme.md contains instructions on getting the non-secure(b_u585i_iot02a_ntz) version of the project up and running. It connects to AWS IoT Core and publishes sensor data.

## Software Components

### Littlefs
The littlefs library is used as a flash filesystem to demonstrate the external Octal-SPI NOR flash available on the b_u585i_iot02a board.

The littlefs port for this board can be found in the [Src/fs](Src/fs) directory.

### CorePKCS11
The CorePKCS11 library is used to ease integration and simulate the PKCS11 API that a secure element sdk might provide by handling cryptographic opoerations with mbedtls.

The CorePKSA11 PAL implementation that is used in the port can be found in the [Src/crypto/core_pkcs11_pal_littlefs.c](Src/crypto/core_pkcs11_pal_littlefs.c) file.

### FreeRTOS OTA Platform Abstraction Layer implementation

## Flash Memory Layout

## Importing the projects into STM32CubeIDE

The b_u585i_iot02a_ntz project does not use the TrustZone capabilities of the STM32U5 microcontroller.

Follow the instructions in the repository [README.md](../README.md) to import the Projects into STM32CubeIDE.

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
stn32u5_tool.sh flash_ntz
```

## Performing Over-the-air (OTA) Firmware Update

The project shows an IoT reference implementation of how to integrate FreeRTOS libraries on STM32U5 platform to perform OTA update with AWS IoT using the *non trustzone* hardware capablities.

The demo runs FreeRTOS OTA agent as one of the RTOS tasks in background, which waits for OTA updates from cloud.

The non-trustzone version of the demo leverages the dual-bank architecutre of the internal flash memory. The 2MB internal flash is split into two banks of 1MB each.

While the main firmware is running on one bank, an ota update is installed on the second bank.
