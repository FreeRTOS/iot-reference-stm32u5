# Getting Started Guide for STM32U5 IoT Discovery Kit with AWS

## Overview

This project demonstrates how to integrate modular FreeRTOS software with hardware enforced security to create secure and updatable cloud connected applications. The project is pre-configured to run on the STM32U585 IoT Discovery Kit and connect to AWS.

The STM32U585 IoT Discovery kit comes with 2048 KB of Flash memory, 786 kB of RAM and is based on Arm Cortex®-M33.

The STM32U5 IoT Discovery Kit is equipped with a Wi-Fi and Bluetooth module, microphones, a temperature and humidity sensor, a magnetometer, an accelerometer and gyroscope, a pressure sensor, as well as Time-of-Flight (ToF) and gesture-detection sensors.

The board also comes with 512-Mbit octal-SPI Flash memory, 64-Mbit octal-SPI PSRAM, 256-Kbit I2C EEPROM, as well as ARDUINO Uno V3, STMod+, and Pmod expansion connectors, plus an expansion connector for a camera module, and STLink-V3E embedded debugger.

The following project folder consists of a **non secure version(b_u585i_iot02a_ntz)** of the project and **secure version(b_u585i_iot02a_tfm)** of the project. The following shows the steps to follow for connecting the secure project to AWS and doing an OTA(Over the Air) Update and also connecting the non secure project to AWS. 

## Hardware Description

https://www.st.com/en/microcontrollers-microprocessors/stm32u5-series.html

##  User Provided items

A USB micro-B cable

## Clone the repository and submodules

Using your favorite unix-like console application, run the following commands to clone and initialize the git repository and its submodules, preferably on the C drive directly. For the purpose of this document it has been cloned on the C drive. 

```
git clone https://github.com/FreeRTOS/lab-iot-reference-stm32u5.git
git -C lab-iot-reference-stm32u5 submodule update --init
```

## Set up your Development Environment

Download and install [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html#get-software) Version 1.8.0.
Download and install the latest version of [Python](https://www.python.org/downloads/).
During the installation process for python, make sure to tick the boxes to install pip and add python to path.
To install python libraries using pip, navigate to the repository (C:\lab-iot-reference-stm32u5\tools) and type:

```
pip install -r requirements.txt
```
The above command will install the following packages-boto3,requests,pyserial,cryptography and black required for the build.

Install [AWS CLI](https://docs.aws.amazon.com/cli/latest/userguide/getting-started-install.html).

Create an [IAM user](https://docs.aws.amazon.com/IAM/latest/UserGuide/id_users_create.html).

Type :

```
aws configure
```
on a command prompt terminal. Fill in the AWS Access Key ID, AWS Secret Access Key, Default output format and Region as show below:

 <img width="371" alt="12" src="https://user-images.githubusercontent.com/44592967/153652474-eaa0f45e-654f-4eb0-986e-edce6d1af53f.PNG">

Optional: A serial terminal like [TeraTerm](https://osdn.net/projects/ttssh2/releases/)

## Set up your hardware

![image](https://user-images.githubusercontent.com/44592967/162077566-531f1bf3-d974-44ef-9409-06df1615cfd0.png)

Connect the ST-LINK USB port (USB STLK / CN8) to the PC with USB cable.  The USB STLK port is located to the right of the MXCHIP module in the above figure. It is used for power supply, programming the application in flash memory, and interacting with the application with virtual serial COM port. 

## Running the non secure and the secure project  

For getting the Non Trust Zone project up and running follow the README.md in  
..\lab-iot-reference-stm32u5\Projects\b_u585i_iot02a_ntz.

For getting the Trust Zone project up and running follow the README.md in  
..\lab-iot-reference-stm32u5\Projects\b_u585i_iot02a_tfm.

## Component Licensing

Source code located in the *Projects*, *Common*, *Middleware/AWS*, and *Middleware/FreeRTOS* are available under the terms of the MIT License. See the LICENSE file for more details.

Other libraries located in the *Drivers* and *Middleware* directories are available under the terms specified in each source file.
