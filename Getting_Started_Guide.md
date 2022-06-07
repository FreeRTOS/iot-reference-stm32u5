# Setting Up your Development Environment

## Step 1: Setup your development board

![b_u585_iot02a](https://user-images.githubusercontent.com/1633960/164777317-d9f922cf-8019-4b29-8145-c92b0c4a5b85.png)

1. Verify that the 5V_USB_STL and JP3 jumpers are bridged and the remaining jumpers are not bridged.
2. Check that the BOOT0 switch is in the position closest to the STLINK USB connector.
3. Connect a USB micro-B cable between the USB_STLK connector and your computer.

The USB STLK port is located to the right of the MXCHIP WiFi module in the figure. It is used for power supply, programming, debugging, and interacting with the application via UART over USB.

### Update the WiFi module Firmware
Depending on the board revision in use, you may need to update the wifi firmware for your board. For more information, visit the [EMW3080](https://www.st.com/en/development-tools/x-wifi-emw3080b.html) page on the ST Microelectronics website.

1. Download the [EMW3080 update tool](https://www.st.com/content/ccc/resource/technical/software/firmware/group1/48/a2/e8/27/7f/ae/4b/26/x-wifi-emw3080b/files/x-wifi-emw3080b.zip/jcr:content/translations/en.x-wifi-emw3080b.zip) from the STMicroelectronics website.
2. Unzip the archive.
3. Flip the SW1_BOOT0 switch to the **1** / left position.
4. Press the RST button to reset the STM32U5 MCU.
5. Drag and drop the *EMW3080updateV2.1.11RevC.bin* binary from the archive that was unzipped in step 2 to the **DIS_U585AI** usb mass storage device.
6. Wait for the mass storage device to disconnect and then reconnect.
7. Return SW1_BOOT0 to the **0** / right position.
8. Switch the SW2 BOOT switch to the **0** position.
9. Press the RST button again.
10. Connect a serial terminal program to the STlink USB->UART port.
> Note: You may need to remap line endings for it to display correctly.
>
> Input: LF -> CRLF
>
> Output CR -> CRLF
>
> For picocom, the correct mapping arguments are: ```--imap lfcrlf --omap crcrlf```
11. Press the blue "USER" button and wait for the firmware update to complete.
12. Switch the SW2 BOOT switch back to the **1** position.

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

#### Windows: Installation with Scoop Package Manager
With [scoop](https://scoop.sh/) package manager installed, run the following command from your preferred shell.
```
scoop install python cmake perl
```
#### Windows: Installation with Chocolatey Package Manager
With [chocolatey](https://chocolatey.org/install) installed, run the following commands from your preferred shell.
```
choco install cmake
choco install wget
choco install git
choco install python3
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

1. Locate your preferred version of bash.exe and determine the windows path to it.

    For reference, the default location for GitBash is ```C:\Program Files\Git\bin```.

2. Run the following command to open the environment variable editor from Control Panel:
```
rundll32 sysdm.cpl,EditEnvironmentVariables
```

3. Select the "Path" user environment variable and click "Edit".

4. Select "New" and then paste the path to the directory containing bash.exe found above.

5. Press OK and OK to exit the environment variable editor.

6. Log out of your windows session and then log back in to allow the environment variable changes to take effect.

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

#### Link GNU core utilities into system bin directory
```
sudo ln -s /usr/local/Cellar/coreutils/9.0_1/bin/realpath /usr/local/bin/realpath
sudo ln -s /usr/local/Cellar/coreutils/9.0_1/bin/readlink /usr/local/bin/readlink
```

## Step 3: Clone the repository and submodules
Using your favorite unix-like console application, run the following commands to clone and initialize the git repository and it's submodules:
```
git clone https://github.com/FreeRTOS/lab-iot-reference-stm32u5.git
git -C lab-iot-reference-stm32u5 submodule update --init
```

## Step 4: Setup your AWS account with awscli

Follow the instructions to [Create an IAM user](https://docs.aws.amazon.com/iot/latest/developerguide/setting-up.html).

Run the following command to set up the aws cli.
```
aws configure
```

Fill in the AWS Access Key ID, AWS Secret Access Key, and Region based on the IAM user your created in the previous step.

If you have already configured your AWS account, you may accept the existing default values listed in [brackets] by pressing the enter key.

```
$ aws configure
AWS Access Key ID []: XXXXXXXXXXXXXXXXXXXX
AWS Secret Access Key []: YYYYYYYYYYYYYYYYYYYY
Default region name [us-west-2]:
Default output format [json]:
```

## Step 5: Install STM32CubeIDE
Download the latest version of STM32CubeIDE from the [STMicroelectronics website](https://www.st.com/en/development-tools/stm32cubeide.html).

At the time of this writing, Version 1.9.0 was the latest release:
- [Windows][ide_url_windows]
- [Mac OS][ide_url_mac]
- [Debian Package bundle][ide_url_deb]
- [Redhat Package bundle][ide_url_rpm]
- [Generic Linux Bundle][ide_url_lin]

Abridged installation instructions are included below. Please refer to the [STM32CubeIDE Installation guide](https://www.st.com/resource/en/user_manual/um2563-stm32cubeide-installation-guide-stmicroelectronics.pdf) and the included instructions for your platform if additional help is needed.

The projects in this repository have been verified with versions 1.8.0 and 1.9.0 of STM32CubeIDE.

### Windows Normal Install
1. Download the [STM32CubeIDE windows zip archive][ide_url_windows].
2. Unzip the package by double-clicking.
3. Run the extracted installer executable.

### Ubuntu Linux, Debian Linux, etc (deb package)
Open a terminal window and follow the steps below to install STM32CubeIDE on a Debian based Linux machine.

Download the STM32CubeIDE Linux generic installer package
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
Open a terminal window and follow the steps below to install STM32CubeIDE on a Redhat based linux machine.

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

## Step 6: Import Projects into STM32CubeIDE
1. Open STM32CubeIDE.
2. When asked to open a workspace directory, select the location in which you cloned this git repository.

> Note: If you are not asked to select a workspace when STM32CubeIDE start, you may access this dialog via the ***File -> Switch Workspace -> Other*** menu item.
3. Select ***File -> Import***.
4. Select ***General -> Existing Projects Into Workspace*** in the ***Select an Import Wizard*** dialog and click **Next >**.
5. Click **Browse** next to the *Select root directory* box and navigate to the root of this repository.
6. Click the check box next to both the *b_u585i_iot02a_ntz* and *b_u585i_iot02a_tfm* projects and then click **Finish**.
> Note: Ensure that *copy projects into workspace* is not selected

## Step 7: Build Firmware image and Flash your development board
After importing the two demo projects into STM32CubeIDE, decide which one you will build and deploy first and follow the instructions below to do so.

### Building
In the **Project Explorer** pane of STM32CubeIDE, Double click on the project to open it.

Next, Right-click on the project in the **Project Explorer** pane and select **Build Project**

> Note: You may also build the current project using the **Project**->**Build Project** menu item.

### Non-TrustZone Project
Review the README.md file for the [Non TrustZone](Projects/b_u585i_iot02a_ntz) project for more information on the setup and limitations of this demo project.

To flash the b_u585i_iot02a_ntz project to your STM32U5 IoT Discovery Kit, select the *Flash_ntz* configuration from the **Run Configurations** menu.

### TrustZone / TF-M Enabled Project
Review the README.md file for the [TrustZone Enabled](Projects/b_u585i_iot02a_tfm) project for more information on the setup and limitations of this demo project.

To flash the b_u585i_iot02a_tfm project to your STM32U5 IoT Discovery Kit, select the *Flash_tfm_bl2_s_ns* configuration from the **Run Configurations** menu.

## Step 8: Provision Your Board

### Option 8A: Provision automatically with provision.py

The simplest way to provision your board is to run the tools/provision.py script.

After sourcing the tools/env_setup.sh script, run the folloing command:
> Note: When running interactive python scripts in GitBash on Microsoft Windows, you must add "winpty " to the beginning of the command you wish to run.
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
The provision.py script has a variety of command line options that may be used to modify it's behavior.

The *--verbose* option is particularly useful for debugging.

The *--cert-issuer* option may be set to either *self* to generate a self-signed certificate on the device or *aws* to generate a Certificate Signing Request and issue the cert using the AWS IoT CreateCertificateFromCsr API.

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

### Option 8B: Provision manually via CLI
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

# Observe MQTT messages on the AWS IoT Core Console.

Log in to [aws.amazon.com](aws.amazon.com) with the IAM User created earlier in this guide.

Navigate to the **Iot Core** service using the search box at the top of the page.

Using the menu on the left side of the screen, select **Test**->**MQTT test client**

Set the topic filter to *#* and click the *Subscribe* button.

<img width="500" alt="23" src="https://user-images.githubusercontent.com/44592967/153659120-264158f3-3cc1-4062-9094-c6c5d469d130.PNG">

You will soon see sensor data streaming from your test device.
> Note: You may need to reset the board using the black *RST* button.

<img width="271" alt="24" src="https://user-images.githubusercontent.com/44592967/153659267-9de9ac07-bd3b-4899-a7ce-044aa3ba678a.PNG">


# Setting up FreeRTOS OTA

## Generate a Code Signing key

Devices uses digital signatures to verify the authenticity of the firmware updates sent over the air. Images are signed by an authorized source who creats the image, and device can verify the signature of the image, using the corresponding public key of the source. Steps below shows how to setup and provision the code signing credentials so as to enable cloud to digitally sign the image and the device to verify the image signature before boot.

1. In your working directory, use the following text to create a file named *cert_config.txt*. Replace *test_signer@amazon.com* with your email address:

```
[ req ]
prompt             = no
distinguished_name = my_dn

[ my_dn ]
commonName = test_signer@amazon.com

[ my_exts ]
keyUsage         = digitalSignature
extendedKeyUsage = codeSigning
```
2. Create an ECDSA code-signing private key:

```
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -pkeyopt ec_param_enc:named_curve -outform PEM -out ecdsasigner-priv-key.pem
```

3. Generate the corresponding public key from the private key:
```
openssl ec -inform pem -in ecdsasigner-priv-key.pem -pubout -outform pem -out ecdsasigner-pub-key.pem
```

4. Create an ECDSA code-signing certificate to be uploaded to the AWS ACM service:

```
openssl req -new -x509 -config cert_config.txt -extensions my_exts -nodes -days 365 -key ecdsasigner-priv-key.pem -out ecdsasigner.crt

```

5. Import the code-signing certificate and private key into AWS Certificate Manager:

> Note: This command displays an ARN for your certificate. You will need this ARN when you create an OTA update job later.

```
aws acm import-certificate --certificate fileb://ecdsasigner.crt --private-key fileb://ecdsasigner-priv-key.pem
```

6. Connect to the target device via a serial terminal. On the command line prompt type following command to import the OTA signing key:

```
> pki import key ota_signer_pub
# Press `Enter` then paste the contents of the PEM public key file `ecdsasigner-pub-key.pem` into the terminal.
# Press `Enter` again.
```

> Note: `ota_signer_pub` is the label used to refer to the code signing key during verification of the firmware update.

7. Create a signing profile in AWS to sign the firmware image.

```
aws signer put-signing-profile --profile-name <your profile name> --signing-material certificateArn=<certificate arn created in step 4> --platform AmazonFreeRTOS-Default --signing-parameters certname=ota_signer_pub
```

## Setup OTA S3 bucket, Service role and policies in AWS

1. S3 bucket is used to store the new firmware image to be updated. To create a new S3 bucket follow these steps here: https://docs.aws.amazon.com/freertos/latest/userguide/dg-ota-bucket.html

2. Create a service role which grants permission for OTA service to access the firmware image: https://docs.aws.amazon.com/freertos/latest/userguide/create-service-role.html

3. Create an OTA update policy using the documentatio here: https://docs.aws.amazon.com/freertos/latest/userguide/create-ota-user-policy.html

4. Add a policy for AWS IoT to access the code signing profile: https://docs.aws.amazon.com/freertos/latest/userguide/code-sign-policy.html

## Create a code signed firmware update job

1. Bump up the version of the new firmware image to be updated. From the demo project, open File `Src/ota_pal/ota_firmware_version.c` and set APP_VERSION_MAJOR to 1 higher than current version. Build the firmware image using STM32Cube IDE.

2. Upload the new image to the s3 bucket created in the previous section.

```
aws s3 cp <image binary path> s3://<s3 bucket for image>/

```

Get the latest s3 file version of the binary image by executing the command below. The command returns an array of json structs containing details of all version. To get the latest version ID, look for *VersionId* field in the json struct where *isLatest* field is *true*.

```
aws s3api  list-object-versions --bucket <s3 bucket for image> --prefix <image binary name>
```

3. Create a new OTA Update job configuration json file (Example: ota-update-job-config.json) in your filesystem as below. Substitue the parameters with the output obtained from steps above.

```
{
     "otaUpdateId": "<A unique job ID for the OTA job>",
     "targets": [
         "arn:aws:iot:<region>:<account id>:thing/<thing name>"
     ],
     "targetSelection": "SNAPSHOT",
     "files": [{
         "fileName": "<image binary name>",
         "fileVersion": "1",
         "fileLocation": {
             "s3Location": {
                 "bucket": "<s3 image bucket created above>",
                 "key": "<image binary name>",
                 "version": "<latest s3 file version of binary image>"
             }
         },
         "codeSigning": {
             "startSigningJobParameter": {
                 "signingProfileName": "<signing profile name>",
                 "destination": {
                     "s3Destination": {
                         "bucket": "<s3 image bucket created above>"
                     }
                 }
             }
         }
     }],
     "roleArn": "<ARN of the OTA service role created above>"
 }

```

Create a new OTA update job from the configuration file:
```
aws iot create-ota-update --cli-input-json file://<ota job configuration file path in your filesystem>
```

The command on success returns the OTA Update identifier and status of the Job as `CREATE_PENDING`. To get the corresponding job ID of the OTA Job, execute the following command and look for `awsIotJobId` field in json document returned.

```
aws iot get-ota-update --ota-update-id=<ota update id created above>
```
Note down the job ID to check the status of the job later.


#### Monitoring and Verification of firmware update

 Once the job is created on the terminal logs, you will see that OTA job is accepted and device starts downloading image.


 Create a new OTA update job from the configuration file:

 ```
 aws iot create-ota-update --cli-input-json file://<ota job configuration file path in your filesystem>
 ```

 The command on success returns the OTA Update identifier and status of the Job as *CREATE_PENDING*. To get the corresponding job ID of the OTA Job, execute the following command and look for *awsIotJobId* field in json document returned.

 ```
 aws iot get-ota-update --ota-update-id=<ota update id created above>
 ```
 Note down the job ID to check the status of the job later.

 ## Monitoring and Verification of firmware update

 1. Once the job is created on the terminal logs, you will see that OTA job is accepted and device starts downloading image.

```
<INF>    16351 [OTAAgent] Current State=[WaitingForFileBlock], Event=[RequestFileBlock], New state=[WaitingForFileBlock] (ota.c:2834)
<INF>    15293 [OTAAgent] Extracted parameter: [key: value]=[execution.jobDocument.afr_ota.streamname: AFR_OTA-eb53bc47-6918-4b2c-9c85-a4c74c44a04c] (ota.c:1642)
<INF>    15294 [OTAAgent] Extracted parameter: [key: value]=[execution.jobDocument.afr_ota.protocols: ["MQTT"]] (ota.c:1642)
<INF>    15296 [OTAAgent] Extracted parameter: [key: value]=[filepath: b_u585i_iot02a_ntz.bin] (ota.c:1642)
<INF>    17784 [OTAAgent] Current State=[WaitingForFileBlock], Event=[RequestFileBlock], New state=[WaitingForFileBlock] (ota.c:2834)
<INF>    15297 [OTAAgent] Extracted parameter: [key: value]=[fileid: 0] (ota.c:1683)
<INF>    15298 [OTAAgent] Extracted parameter: [key: value]=[certfile: ota_signer_pub] (ota.c:1642)
<INF>    15300 [OTAAgent] Extracted parameter [ sig-sha256-ecdsa: MEUCIGWRkFqcumdPZhoZ93ov5Npvsjj7... ] (ota.c:1573)
<INF>    15301 [OTAAgent] Extracted parameter: [key: value]=[fileType: 0] (ota.c:1683)
<INF>    15301 [OTAAgent] Job document was accepted. Attempting to begin the update. (ota.c:2199)
<INF>    16533 [OTAAgent] Number of blocks remaining: 306 (ota.c:2683)
<INF>    15450 [OTAAgent] Setting OTA data interface. (ota.c:938)
<INF>    15450 [OTAAgent] Current State=[Creating

```
2. Once the full image has been downloaded, the OTA library verifies the image signature and activates the new image in the unused flash bank.

```
<INF>    67405 [OTAAgent] Received final block of the update. (ota.c:2633)
<INF>    67405 [OTAAgent] Validating the integrity of OTA image using digital signature. (ota_pal.c:681)
<INF>    69643 [OTAAgent] Received entire update and validated the signature. (ota.c:2654)
```

3. New image boots up and performs a self test, here it checks the version is higher than previous. If so it sets the new image as valid.

```
<INF>    15487 [OTAAgent] In self test mode. (ota.c:2102)
<INF>    15487 [OTAAgent] New image has a higher version number than the current image: New image version=1.9.0, Previous image version=0.9.0 (ota.c:1932)
```
4. Checking the job status, should show the job as succeeded:

```
aws iot describe-job-execution --job-id=<Job ID created above> --thing-name=<thing name>
```
