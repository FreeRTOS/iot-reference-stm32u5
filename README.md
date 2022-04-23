# Getting Started with the FreeRTOS STM32U5 IoT Reference

This project demonstrates how to integrate modular [FreeRTOS kernel](https://www.freertos.org/RTOS.html) and [libraries](https://www.freertos.org/libraries/categories.html) with hardware enforced security to build more secure updatable cloud connected applications. The project is configured to run on the STM32U585 IoT Discovery Kit and connect to AWS IoT services.

The *Projects* directory consists of a [Non-TrustZone](Projects/b_u585i_iot02a_ntz) and a [Trusted-Firmware-M-Enabled](Projects/b_u585i_iot02a_tfm) project which both demonstrate connecting to AWS IoT Core and utilizing many of the services available via the MQTT protocol.

This includes demonstration tasks for the following AWS services:
* [AWS IoT Device Shadow](https://docs.aws.amazon.com/iot/latest/developerguide/iot-device-shadows.html)
* [AWS IoT Device Defender](https://docs.aws.amazon.com/iot/latest/developerguide/device-defender.html)
* [AWS IoT Jobs](https://docs.aws.amazon.com/iot/latest/developerguide/iot-jobs.html)
* [MQTT File Delivery](https://docs.aws.amazon.com/iot/latest/developerguide/mqtt-based-file-delivery.html)
* [AWS IoT OTA Update](https://docs.aws.amazon.com/freertos/latest/userguide/freertos-ota-dev.html)

The demo projects both connect to AWS IoT core via the included Wi-Fi module and use the [CoreMQTT-Agent](https://github.com/FreeRTOS/coreMQTT-Agent) library to share a single MQTT connection among multiple tasks. These tasks publish data from a subset of the sensor available on the development board, and demonstrate use of the AWS IoT Device Shadow and Device Defender services.

## Hardware Description
The [STM32U585 IoT Discovery kit](https://www.st.com/en/evaluation-tools/b-u585i-iot02a.html) integrates an [STM32U585AII6Q](https://www.st.com/en/microcontrollers-microprocessors/stm32u585ai.html) ARM Cortex-M33 microcontroller with 2048 KB of internal flash memory, 786 kB of SRAM, and the latest in security features.

In addition to the STM32U5 microcontroller, the STM32U5 IoT Discovery Kit is equipped with a variety of sensors including:
* 2 x [MP23DB01HPTR](https://www.st.com/en/mems-and-sensors/mp23db01hp.html) MEMS Microphones
* 1 x [HTS221](https://www.st.com/en/mems-and-sensors/hts221.html) Capacitive Humidity and Temperature sensor
* 1 x [IIS2MDCTR](https://www.st.com/en/mems-and-sensors/iis2mdc.html) 3-Axis Magnetometer
* 1 x [ISM330DHCX](https://www.st.com/en/mems-and-sensors/ism330dhcx.html) 3D Accelerometer and Gyroscope
* 1 x [LPS22HH](https://www.st.com/en/mems-and-sensors/lps22hh.html) MEMS Barometric Pressure sensor.
* 1 x [VL53L5CXV0GC/1](https://www.st.com/en/imaging-and-photonics-solutions/vl53l5cx.html) Time Of Flight Ranging sensor.
* 1 x [VEML6030](https://www.vishay.com/optical-sensors/list/product-84366/) Ambient Light sensor


In addition, the STM32U5 Discovery Kit is also equipped with the following external peripherals:
* 1 x [EMW3080B](https://www.st.com/en/development-tools/x-wifi-emw3080b.html) WiFi Module
* 1 x [STM32WB5MMGH6TR](https://www.st.com/en/microcontrollers-microprocessors/stm32wb5mmg.html) Bluetooth Module
* 1 x [M24128-DFMC6TP](https://www.st.com/en/memories/m24128-df.html) 128kb I2C EEPROM
* 1 x [STSAFE-A110](https://www.st.com/en/secure-mcus/stsafe-a110.html) Secure Element
* 1 x [TCPP03](https://www.st.com/en/protections-and-emi-filters/tcpp03-m20.html) USB-C controller
* 1 x [MX25LM51245GXDI005](https://www.macronix.com/en-us/products/NOR-Flash/Serial-NOR-Flash/Pages/spec.aspx?p=MX25LM51245G&m=Serial%20NOR%20Flash&n=PM2357) 512 Mb (64 MB) Octal-SPI NOR Flash.
* 1 x [APS6408L-3OB-BA](http://www.apmemory.com/wp-content/uploads/APM_PSRAM_E3_OPI_Xccela-APS6408L-3OBMx-v3.6-PKG.pdf) 64 Mbit (8 MB) Octo-SPI PSRAM
* 1 x STLINK-V3E Debug Interface

The STM32U5 Discovery Kit also includes the following expansion connectors:
* 1 x ARDUINO Uno V3 compatible connector
* 2 x STMod+ connectors
* 1 x Pmod expansion connector
* 1 x ST [MB1379](https://www.st.com/en/development-tools/b-cams-omv.html) camera module connector

For more information on the STM32U585 IoT Discovery Kit and B-U585I-IOT02A development board, please refer to the following resources:
* [B-U585I-IOT02A Product Page](https://www.st.com/en/evaluation-tools/b-u585i-iot02a.html)
* [B-U585I-IOT02A Product Specification](https://www.st.com/resource/en/data_brief/b-u585i-iot02a.pdf)
* [B-U585I-IOT02A User Manual](https://www.st.com/resource/en/user_manual/um2839-discovery-kit-for-iot-node-with-stm32u5-series-stmicroelectronics.pdf)
* [B-U585I-IOT02A Schematic](https://www.st.com/resource/en/schematic_pack/mb1551-u585i-c02_schematic.pdf)

For more details about the STM32U5 series of microcontrollers, please refer to the following resources:
* [STM32U5 Series Product Page](https://www.st.com/en/microcontrollers-microprocessors/stm32u5-series.html)
* [STM32U585xx Datasheet](https://www.st.com/resource/en/datasheet/stm32u585ai.pdf)
* [RM0456 STM32U575/575 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0456-stm32u575585-armbased-32bit-mcus-stmicroelectronics.pdf)
* [STM32U575xx and STM32U585xx Device Errata ](https://www.st.com/resource/en/errata_sheet/es0499-stm32u575xx-and-stm32u585xx-device-errata-stmicroelectronics.pdf)

## Key Software Components

### Logging

### Command Line Interface (CLI)

### Key-Value Store
The key-value store located in the Common/kvstore directory is used to store runtime configuration values in non-volatile flash memory.
By default, the kvstore interface can be used to read and write the following items:
* WiFi SSID
* WiFi Password
* Thing Name (MQTT Device ID)
* MQTT Endpoint
* MQTT Port
* Time High Water Mark.

The kvstore api is accessible via the CLI using the "conf" command.
```
> help conf
conf:
    Get/ Set/ Commit runtime configuration values
    Usage:
    conf get
        Outputs the value of all runtime config options supported by the system.

    conf get <key>
        Outputs the current value of a given runtime config item.

    conf set <key> <value>
        Set the value of a given runtime config item. This change is staged
        in volatile memory until a commit operation occurs.

    conf commit
        Commit staged config changes to nonvolatile memory.
```

### PKI Object
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

### Mbedtls Transport
The *Common/net/mbedtls_transport.c* file contains a transport layer implementation for coreMQTT and coreHTTP which uses mbedtls to encrypt the connection in a way supported by AWS IoT Core.

Optionally, client key / certificate authentication may be used with the mbedtls transport or this parameter may be set to NULL if not needed.

# Setting Up your Development Environment

## Step 1: Setup your development board

![b_u585_iot02a](https://user-images.githubusercontent.com/1633960/164777317-d9f922cf-8019-4b29-8145-c92b0c4a5b85.png)

1. Verify that the 5V_USB_STL and JP3 jumpers are bridged and the remaining jumpers are not bridged.
2. Check that the BOOT0 switch is in the position closest to the STLINK USB connector.
3. Connect a USB micro-B cable between the USB_STLK connector and your computer.

The USB STLK port is located to the right of the MXCHIP WiFi module in the figure. It is used for power supply, programming, debugging, and interacting with the application via UART over USB.

[ide_url_windows]: https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/1e/53/08/15/0f/e2/4c/a6/stm32cubeide_win/files/st-stm32cubeide_1.9.0_12015_20220302_0855_x86_64.exe.zip/jcr:content/translations/en.st-stm32cubeide_1.9.0_12015_20220302_0855_x86_64.exe.zip

[ide_url_mac]: https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/05/31/11/3e/76/1d/40/01/stm32cubeide_mac/files/st-stm32cubeide_1.9.0_12015_20220302_0855_x86_64.dmg.zip/jcr:content/translations/en.st-stm32cubeide_1.9.0_12015_20220302_0855_x86_64.dmg.zip

[ide_url_rpm]: https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/e1/03/5a/6d/f4/f5/45/2c/stm32cubeide_rpm/files/st-stm32cubeide_1.9.0_12015_20220302_0855_amd64.rpm_bundle.sh.zip/jcr:content/translations/en.st-stm32cubeide_1.9.0_12015_20220302_0855_amd64.rpm_bundle.sh.zip

[ide_url_deb]: https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/13/d4/6b/b0/d2/fd/47/6d/stm32cubeide_deb/files/st-stm32cubeide_1.9.0_12015_20220302_0855_amd64.deb_bundle.sh.zip/jcr:content/translations/en.st-stm32cubeide_1.9.0_12015_20220302_0855_amd64.deb_bundle.sh.zip

[ide_url_lin]: https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/6c/ed/b5/05/5d/2f/44/f3/stm32cubeide_lnx/files/st-stm32cubeide_1.9.0_12015_20220302_0855_amd64.sh.zip/jcr:content/translations/en.st-stm32cubeide_1.9.0_12015_20220302_0855_amd64.sh.zip

## Step 2: Install Prerequisites Packages
Using your platform's package manager, install the following prerequisites:
- Python 3.10 with pip
- perl
- cmake
- git
- libusb
- 7zip (optional, for portable installation of STM32CubeIDE)

### Windows
There are many options for downloading and installing packages on windows. Use the approach you are most comfortable with.

#### Windows: Decide which shell you will use
Windows has a wide variety of unix or posix-like shells available. This guide will assume you are using GitBash which is included in the git for windows package.

#### Windows: Manual Installation without a Package Manager
Download and install the latest version of each of the following packages:
- [python](https://www.python.org/downloads/) (with pip)
- [perl](https://www.perl.org/get.html)
- [cmake](https://cmake.org/download/)
- [git](https://git-scm.com/downloads)
- [7-zip](https://www.7-zip.org/download.html)

### #Windows: Installation with Scoop Package Manager
With [scoop](https://scoop.sh/) package manager installed, run the following command from your preferred shell.
```
scoop install python cmake git ninja 7zip perl
```
#### Windows: Installation with Chocolatey Package Manager
With [chocolatey](https://chocolatey.org/install) installed, run the following commands from your preferred shell.
```
choco install cmake
choco install wget
choco install git
choco install python3
choco install 7zip
choco install perl
```
#### Windows: Enable the git longpaths option:
On windows, long path names can present problems with some programs that utilize legacy APIs. Enable long path support in git so that git knows long paths are supported by STM32CubeIDE and the included toolchain.

Open GitBash or a similar unix-like shell environment and run the following command:
```
git config --system core.longpaths true
```
#### Windows: Add bash.exe to your Path:
In order to use the stm32u5_tool.sh script and the related STM32CubeIDE launch files, you must include bash.exe in your system path.

Locate your preferred version of bash.exe and determine the windows path to it.

For reference, the default location for GitBash is ```C:\Program Files\Git\bin```.

1. Run the following command to open the environment variable editor from Control Panel:
```
rundll32 sysdm.cpl,EditEnvironmentVariables
```

2. Select the "Path" user environment variable and click "Edit".

3. Select "New" and then paste the path to the directory containing bash.exe found above.

4. Press OK and OK to exit the environment variable editor.

5. Log out of your windows session and then log back in to allow the environment variable changes to take effect.

### Linux
Install dependencies using your distribution's package manager:
#### Debian based (.deb / apt)
```
sudo apt install build-essential cmake python3 git libncurses5 libusb-1.0-0-dev
```
#### Redhat (.rpm / dnf / yum)
```
sudo dnf install -y cmake python3 git ncurses-libs libusb
sudo dnf groupinstall -y "Development Tools" "Development Libraries" --skip-broken
```

### Mac OS
#### With Homebrew package manager
Install the hombrew package manager from [brew.sh](https://brew.sh/)
```
brew install python cmake git libusb greadlink coreutils
```

##### Link GNU core utilities into system bin directory
```
sudo ln -s /usr/local/Cellar/coreutils/9.0_1/bin/realpath /usr/local/bin/realpath
sudo ln -s /usr/local/Cellar/coreutils/9.0_1/bin/readlink /usr/local/bin/readlink
```

## Step 3: Clone the repository and submodules
Using your favorite unix-like console application, run the following commands to clone and initialize the git respository and it's submodules:
```
git clone https://github.com/FreeRTOS/lab-iot-reference-stm32u5.git
git -C lab-iot-reference-stm32u5 submodule update --init
```

## Step 4: Setup the workspace python virtual environment
Setup your workspace python environment by following the instructions below. This will install all of the necessary python libraries in a self-contained python [virtualenv](https://virtualenv.pypa.io/en/latest/).

Navigate to the root of the git repository:
```
cd /path/to/lab-iot-reference-stm32u5
```

Install the virtualenv package
```
pip3 install virtualenv
```

Source the setup script to enter the python virutal environment
```
source tools/env_setup.sh
```

## Step 5: Setup your AWS account with awscli

Follow the instructions to [Create an IAM user](https://docs.aws.amazon.com/iot/latest/developerguide/setting-up.html).

After running the env_setup.sh script, run the following command to set up the aws cli.
```
aws configure
```

Fill in the AWS Access Key ID, AWS Secret Access Key, and Region based on the IAM user your created in the previous step.

If you have already configured your AWS acount, you may accept the existing default values listed in [brackets] by pressing the enter key.

```
$ aws configure
AWS Access Key ID []: XXXXXXXXXXXXXXXXXXXX
AWS Secret Access Key []: YYYYYYYYYYYYYYYYYYYY
Default region name [us-west-2]:
Default output format [json]:
```

## Step 6: Install STM32CubeIDE
Download the latest version of STM32CubeIDE from the [STMicroelectronics website](https://www.st.com/en/development-tools/stm32cubeide.html).

At the time of this writing, Version 1.9.0 was the latest release:
- [Windows][ide_url_windows]
- [Mac OS][ide_url_mac]
- [Debian Package bundle][ide_url_deb]
- [Redhat Package bundle][ide_url_rpm]
- [Generic Linux Bundle][ide_url_lin]

Abridged installation instructions are included below. Please refer to the [STM32CubeIDE Installation guide](https://www.st.com/resource/en/user_manual/um2563-stm32cubeide-installation-guide-stmicroelectronics.pdf) and the included instructions for your platform if addiitonal help is needed.

The projects in this repository have been verified with versions 1.8.0 and 1.9.0 of STM32CubeIDE.

### Windows Normal Install
1. Download the [STM32CubeIDE windows zip archive][ide_url_windows].
2. Unarchive it by double-clicking.
3. Run the extracted installer exe.

### Ubuntu Linux, Debian Linux, etc (deb package)
Open a terminal window and follow the steps below to install STM32CubeIDE on a debian based linux machine.

Download the STM32CubeIDE linux generic installer package
```
wget <URL HERE>
```

Extract the package
```
unzip en.st-stm32cubeide_*_amd64.deb_bundle.sh.zip
```

Add execute permissions to the install package
```
chmod +x st-stm32cubeide_*_amd64.deb_bundle.sh
```

Extract the debian packages from the bundle:
```
mkdir -p cubeide_install
./st-stm32cubeide_1.9.0_12015_20220302_0855_amd64.deb_bundle.sh --tar xvf --directory cubeide_install .
```

Install the debian packages
```
export LICENSE_ALREADY_ACCEPTED=1
sudo apt install -y ./cubeide_install/st-stm32cubeide-1.9.0-12015-20220302-0855_amd64.deb ./cubeide_install/st-stlink-udev-rules-1.0.3-2-linux-all.deb ./cubeide_install/st-stlink-server-2.1.0-1-linux-amd64.deb
```

Start the IDE
```
/opt/st/stm32cubeide_1.9.0/stm32cubeide_wayland
# Or
/opt/st/stm32cubeide_1.9.0/stm32cubeide
```

### Redhat derivatives (rpm package)
Open a terminal window and follow the steps below to install STM32CubeIDE on a redhat based linux machine.

Download the [STM32CubeIDE linux rpm installer package][ide_url_rpm]
```
wget <URL HERE>
```

Extract the package
```
unzip en.st-stm32cubeide_*amd64.rpm_bundle.sh.zip
```

Add execute permissions to the install package
```
chmod +x st-stm32cubeide_*amd64.rpm_bundle.sh
```

Start the installation script and follow the prompts on the command line.
```
sudo ./st-stm32cubeide_1.9.0_12015_20220302_0855_amd64.rpm_bundle.sh
```

### Linux - Generic Installer
Open a terminal window and follow the steps below to install STM32CubeIDE on a generic linux machine.

Download the STM32CubeIDE [linux generic installer package][ide_url_lin]:
```
wget <URL>
```

Extract the package
```
unzip en.st-stm32cubeide*amd64.sh.zip
```

Add execute permissions to the install package
```
chmod +x st-stm32cubeide_*amd64.sh
```

Start the installation script and follow the prompts on the command line.
```
./st-stm32cubeide_1.9.0_12015_20220302_0855_amd64.sh
```

### Mac OS
1. Download the [STM32CubeIDE Mac package file][ide_url_mac]:

```
wget <URL>
```

2. Unzip the package:
```
unzip st-stm32cubeide*.dmg.zip
```

3. Mount the resulting dmg disk image by double clicking on it.

4. Install the ST-link and/ or J-link debug tools.

5. Copy the STM32CubeIDE package to your /Applications directory.

5. Some releases of the STM32CubeIDE MacOS application is not properly signed and notarized, so the following command must be run after copying the .app to /Applications.
```
xattr -c /Applications/STM32CubeIDE.app
```

6. Finally, open STM32CubeIDE from the Applications directory.

## Step 7: Import Projects into STM32CubeIDE
1. Open STM32CubeIDE.
2. When asked to open a workspace directory, select the location in which you cloned this git repository.

> Note: If you are not asked to select a workspace when STM32CubeIDE start, you may access this dialog via the ***File -> Switch Workspace -> Other*** menu item.
3. Select ***File -> Import***.
4. Select ***General -> Existing Projects Into Workspace*** in the ***Select an Import Wizard*** dialog and click **Next >**.
5. Click the check box next to both the *b_u585i_iot02a_ntz* and *b_u585i_iot02a_tfm* projects and then click **Next >**.
> Note: Ensure that *copy projects into workspace* is not selected

## Step 8: Build Firmware image and Flash your development board
Follow the instruction on the page linked below for the project configuration you would like to build.

For more information about the [Non Trust Zone](Projects/b_u585i_iot02a_ntz) project along with build and flashing instructions, refer to the [README.md](Projects/b_u585i_iot02a_ntz/README.md) in the [Projects/b_u585i_iot02a_ntz](Projects/b_u585i_iot02a_ntz) directory.

For more information about the [Trust Zone Enabled](Projects/b_u585i_iot02a_tfm) project along with build and flashing instructions, refer to the [README.md](Projects/b_u585i_iot02a_tfm/README.md) in the [Projects/b_u585i_iot02a_tfm](Projects/b_u585i_iot02a_tfm) directory.

## Step 9: Provision Your Board

### Option A: Provision automatically with provision.py

The simplest way to provision your board is to run the tools/provision.py script.

After sourcing the tools/env_setup.sh script, run the folloing command:
> Note: When running interactive python scripts in GitBash, you must prepend "winpty " to the command you wish to run.
```
% python tools/provision.py --interactive
Target device path: /dev/cu.usbmodem143303
Connecting to target...
[ INFO ] Found credentials in shared credentials file: ~/.aws/credentials (credentials.py:load)
Interactive configuration mode: Press return to use defaults (displayed in brackets)
time_hwm[1651013601]: <return>
mqtt_port[8883]: <return>
wifi_ssid[]: my_ssid<return>
wifi_credential[]: password<return>
mqtt_endpoint[xxxxxxxxxxxxxx-ats.iot.us-west-2.amazonaws.com]: <return>
thing_name[xxxxxxxxxxxxxxxx]: <return>
Commiting target configuration...
Generating a new public/private key pair
Generating a self-signed Certificate
Attaching thing: xxxxxxxxxxxxxxxx to principal: arn:aws:iot:us-west-2:XXXXXXXXXXXXXX:cert/XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
Attaching the "AllowAllDev" policy to the device certificate.
Importing root ca certificate: "Starfield Services Root Certificate Authority - G2"
Provisioning process complete. Resetting target device...
```

#### Commandline Options
The provision.py script has a variety of commandline options that may be used to change it's behavior.

The *--verbose* option is particularly useful for debugging.

The *--cert-issuer* option may be either *self* to generate a self-signed certificate on the device or *aws* to generate a Certificate Signing Request and issue the cert using the AWS IoT CreateCertificateFromCsr API.

```
usage: provision.py [-h] [-i] [-v] [-d DEVICE] [--wifi-ssid WIFI_SSID] [--wifi-credential WIFI_CREDENTIAL] [--thing-name THING_NAME]
                    [--cert-issuer {self,aws}] [--aws-profile AWS_PROFILE] [--aws-region AWS_REGION] [--aws-access-key-id AWS_ACCESS_KEY_ID]
                    [--aws-access-key-secret AWS_ACCESS_KEY_SECRET]

optional arguments:
  -h, --help            show this help message and exit
  -i, --interactive
  -v, --verbose
  -d DEVICE, --device DEVICE
  --wifi-ssid WIFI_SSID
  --wifi-credential WIFI_CREDENTIAL
  --thing-name THING_NAME
  --cert-issuer {self,aws}
  --aws-profile AWS_PROFILE
  --aws-region AWS_REGION
  --aws-access-key-id AWS_ACCESS_KEY_ID
  --aws-access-key-secret AWS_ACCESS_KEY_SECRET
  ```

### Option B: Provision manually via CLI
Open the target board's serial port with your favorite serial terminal. Some common options are terraterm, putty, screen, minicom, and picocom. Additionally a serial terminal is included in the pyserial package installed in the workspace python environment.

To use the pyserial utility, run the following command to interactively list available serial devices:
> Note: When running interactive python scripts in GitBash, you must prepend "winpty " to the command you wish to run.

```
% source tools/env_setup.sh
% python -m serial - 115200

--- Available ports:
---  1: /dev/cu.Bluetooth-Incoming-Port 'n/a'
---  2: /dev/cu.usbmodem143303 'STLINK-V3 - ST-Link VCP Data'
--- Enter port index or full name: 2<return>
--- Miniterm on /dev/cu.usbmodem143303  115200,8,N,1 ---
--- Quit: Ctrl+] | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H ---
```

And select your b_u585i_iot02a board from the list by number or port name.
#### Thing Name

First, configure the desired thing name / mqtt device identifier:
```
> conf set thing_name my_thing_name
thing_name="my_thing_name"
```
#### WiFi SSID and Passphrase
Next, configure you WiFi network details:
```
> conf set wifi_ssid ssidGoesHere
wifi_ssid="ssidGoesHere"
> conf set wifi_credential MyWifiPassword
wifi_credential="MyWifiPassword"
```
#### MQTT Endpoint
Next, set the mqtt endpoint to the endpoint for your account:
```
> conf set mqtt_endpoint xxxxxxxxxxxxxx-ats.iot.us-west-2.amazonaws.com
mqtt_endpoint="xxxxxxxxxxxxxx-ats.iot.us-west-2.amazonaws.com"
```
> Note: You can determine the endpoint for your AWS account with the ```aws iot describe-endpoint``` command or on the *Settings* page of the AWS IoT Core console.

#### Commit Configuration Changes
Finally, commit the staged configuration changes to non-volatile memory.
```
> conf commit
Configuration saved to NVM.
```

#### Generate a private key
Use the *pki generate key* command to generate a new ECDSA device key pair. The resulting public key will be printed to the console.
```
> pki generate key
SUCCESS: Key pair generated and stored in
Private Key Label: tls_key_priv
Public Key Label: tls_key_pub
-----BEGIN PUBLIC KEY-----
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX=
-----END PUBLIC KEY-----
```
#### Generate a self-signed certificate
Next, use the *pki generate cert* command to generate a new self-signed certificate:
```
> pki generate cert
-----BEGIN CERTIFICATE-----
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX==
-----END CERTIFICATE-----
```

Save the resulting certificate to a new file.

#### Reset the target device
```
> reset
Resetting device.
```

### Register the device with AWS IoT Core:

#### Register the certificate
Follow the instructions at the AWS IoT Core Developer Guide to [register a client certificate](https://docs.aws.amazon.com/iot/latest/developerguide/manual-cert-registration.html#manual-cert-registration-noca-cli).
```
aws iot register-certificate-without-ca \
    --status ACTIVE \
    --certificate-pem file://device_cert_filename.pem
```

#### Register the Thing Name
```
aws iot create-thing \
    --thing-name SampleIoTThing
```

#### Attach the Certificate to the thing
```
aws iot attach-thing-principal \
    --principal certificateArn \
    --thing-name thingName
```

#### Register a policy if none exists:
```
aws iot create-policy \
    --policy-name="AllowAllDev" \
    --policy-document="{ \"Version\": \"2012-10-17\", \"Statement\": [{\"Effect\": \"Allow\", \"Action\": \"iot:*\", \"Resource\": \"*\"}]}"
```
> Note: This policy allows very broad access to AWS IoT MQTT APIs. Use a more restrictive policy for any production environments.

#### Attach a security policy

```
aws iot attach-policy \
    --target certificateArn \
    --policy-name AllowAllDev
```

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
