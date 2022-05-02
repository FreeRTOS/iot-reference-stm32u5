# Getting Started Guide for STM32U5 IoT Discovery Kit with AWS
The following Readme.md contains instructions on getting the non-secure(b_u585i_iot02a_ntz) version of the project up and running. It connects to AWS Core and publishes data.

## Softweare Components

### Littlefs
The littlefs library is used as a flash filesystem to demonstrate the external Octal-SPI NOR flash available on the b_u585i_iot02a board.

The littlefs port for this board can be found in the [Src/fs](Src/fs) directory.


### CorePKCS11
The CorePKCS11 library is used to ease integration and simulate the PKCS11 API that a secure element sdk might provide by handling cryptographic opoerations with mbedtls.

The CorePKSA11 PAL implementation that is used in the port can be found in the [Src/crypto/core_pkcs11_pal_littlefs.c](Src/crypto/core_pkcs11_pal_littlefs.c) file.

## Importing the projects into STM32CubeIDE

The b_u585i_iot02a_ntz project does not use the TrustZone capabilities of the STM32U5 microcontroller.

Follow the instructions in the repository [README.md](../README.md) to import the Projects into STM32CubeIDE.

## Building and Flashing the Firmware Image

After importing the b_u585i_iot02a_ntz project into STM32CubeIDE, Build the project by highlighting it in the *Project Exporer* pane and then clicking **Project -> Build Project** from the menu at the top of STM32CubeIDE.

To write the newly built image to flash, select the Run As button, then select the `Flash_ntz` target.

## Flashing the Image from the commandline

## Running the demo

Follow the provisioning steps in the main readme.

Optional: Open a serial terminal like TeraTerm. Connect to the board and set the Baud Rate to 115200. Reset the board by clicking on the black reset button on the board to observe activity on TeraTerm.


## Observing activity on AWS

Log in to [aws.amazon.com](aws.amazon.com) with the IAM User created before. On the AWS Management console, Click on Iot Core.  Under Things shown below, click on the name of the thing you entered in the terminal prompt (in this case, stm32u5).

<img width="500" alt="22" src="https://user-images.githubusercontent.com/44592967/153658893-1b57d65f-a71d-4187-a47d-2593c8d98d45.PNG">

Under *Activity*, click on *MQTT Test Client*.

Set the topic filter to # and hit *Subscribe*. Reset the board and observe activity on AWS.  This will allow all topics to come through.

<img width="500" alt="23" src="https://user-images.githubusercontent.com/44592967/153659120-264158f3-3cc1-4062-9094-c6c5d469d130.PNG">

Here is an example of the sensor data coming through:

<img width="271" alt="24" src="https://user-images.githubusercontent.com/44592967/153659267-9de9ac07-bd3b-4899-a7ce-044aa3ba678a.PNG">


## Performing Over-the-air (OTA) Firmware Update

The project shows an IoT reference implementation of how to integrate FreeRTOS libraries on STM32U5 platform to perform OTA update with AWS IoT using the *non trustzone* hardware capablities.

The demo runs FreeRTOS OTA agent as one of the RTOS tasks in background, which waits for OTA updates from cloud.

The non-trustzone version of the demo leverages the dual-bank architecutre of the internal flash memory. The 2MB internal flash is split into two banks of 1MB each.

While the main firmware is running on one bank, an ota update is installed on the second bank.
