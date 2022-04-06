# Getting Started Guide for STM32U5 IoT Discovery Kit with AWS

## Overview

The following Readme.md contains instructions on getting the secure(b_u585i_iot02a_tfm) version of the project up and running. It connects to AWS Core and publishes data.

## Importing the projects into STM32CubeIDE and Building the Project

The b_u585i_iot02a_tfm project uses the TrustZone capabilities of the U5 board.
 With the project cloned on the C drive, open STM32CubeIDE. When prompted with setting workspace, click on Browse and navigate to C:\lab-iot-reference-stm32u5 as shown below:

<img width="550" alt="13" src="https://user-images.githubusercontent.com/44592967/153656131-4688b728-4bde-4828-abdb-12f616b8c70b.PNG">

Note: If the prompt does not come up, look at the Troubleshooting section of this document at the end.

Under Project Explorer, click on *Import projects*.

<img width="550" alt="14" src="https://user-images.githubusercontent.com/44592967/153657055-50aebf12-8e70-428e-beca-913abb1b0e59.PNG">


In the pop–up tab, *Click on Existing projects* into workspace:

<img width="550" alt="15" src="https://user-images.githubusercontent.com/44592967/153657135-369c2501-4e6e-41e7-b3bc-c62fbb323179.PNG">

Hit *Next*. The following prompt will pop up:

<img width="550" alt="16" src="https://user-images.githubusercontent.com/44592967/153657341-fa9c526f-b25d-40cd-ae05-d02e8f3551b5.PNG">

Click on *Browse* above and navigate to the root of the project. Click on *Finish*.

![image](https://user-images.githubusercontent.com/44592967/162328588-15fb92bc-6e21-49cc-b868-b708b0cc33a6.png)

This is how the workspace with the imported projects looks like:

![image](https://user-images.githubusercontent.com/44592967/162328681-f5cd5313-9bf5-4559-8a83-aeb2a655e1ef.png)

Build the project.

The binaries will get populated in the Projects/b_u585_iot02a_tfm/Debug directory as shown below:

![image](https://user-images.githubusercontent.com/44592967/160490860-386ae4d9-8688-432c-b028-235393c9ae13.png)

To Enable TrustZone, Select the "External Tools" menu and run the "Trustzone Enable" tool.

To flash the newly built binary on to the target board, select the "Run" menu and then the "Flash_tfm_bl2_s_ns" configuration.

## Running the demo

With the firmware flashed to the board, open a command prompt, and navigate to the root of the project(lab-iot-reference-stm32u5). Type:

````
python tools/provision.py -i
````
The script will prompt you to enter the following details. You only need to update wifi_ssid, wifi_credential, mqtt_endpoint and thing name.

```
time_hwm[]: <enter>
wifi_credential[]: <your wifi password>
mqtt_endpoint[]: <a1qwhobjt*****-ats.iot.us-east-2.amazonaws.com>
wifi_ssid[]: <your wifi ssid>
mqtt_port[]: <enter>
thing_name[]:
```
<img width="500" alt="20" src="https://user-images.githubusercontent.com/44592967/153658546-8c9a7212-86c9-4b38-aeb8-982aeaade8f0.PNG">

The script will do all the certificate and key provisioning by itself. It will also create a thing with the thing name you entered in the terminal, query your aws account for the correct mqtt endpoint, communicate with AWS, download the certificate and key, and save them to your device.

The end of the script will look somewhat like this:

<img width="500" alt="21" src="https://user-images.githubusercontent.com/44592967/153658687-c6bfc826-5653-483c-97b9-957bef57b53a.PNG">

Optional: Open a serial terminal like TeraTerm. Connect to the board and set the Baud Rate to 115200. Reset the board to observe activity on TeraTerm.


## Troubleshooting

1. Upon opening STM32CubeIDE, if the prompt to set the directory doesn’t come up, Click on File->Switch Workspace->Other, and set the workspace as shown below:

<img width="314" alt="25" src="https://user-images.githubusercontent.com/44592967/153659709-64d8cc21-9c5e-4fe5-8bf0-d43116662f20.PNG">

2. For the command to run the script as mentioned in the document:

`
python tools/provision.py -i –v
`

Note that here are the additional arguments:
```
-h, --help
-i, --interactive
-v, --verbose
-d DEVICE, --device DEVICE
--wifi-ssid WIFI_SSID
--wifi-credential WIFI_CREDENTIAL
--thing-name THING_NAME
--aws-profile AWS_PROFILE
--aws-region AWS_REGION
--aws-access-key-id AWS_ACCESS_KEY_ID
--aws-access-key-secret AWS_ACCESS_KEY_SECRET
```
