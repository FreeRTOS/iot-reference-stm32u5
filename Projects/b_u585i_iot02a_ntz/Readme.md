# Getting Started Guide for STM32U5 IoT Discovery Kit with AWS

## Overview

The following Readme.md contains instructions on getting the non-secure(b_u585i_iot02a_ntz) version of the project up and running. It connects to AWS Core and publishes data. It also contains instructions on how to perform an OTA update in the later section of the Readme.md. 

## Importing the projects into STM32CubeIDE and Building the Project 

The b_u585i_iot02a_ntz project does not use the TrustZone capabilities of the U5 board. 
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

![image](https://user-images.githubusercontent.com/44592967/162328169-c3820c47-a21f-4dac-85fb-e69cd8aa184c.png)

This is how the workspace with the imported project looks like:

![image](https://user-images.githubusercontent.com/44592967/162328220-3d5a6f65-589d-4054-a52b-0f6584f46d68.png)


Build the b_u585i_iot02a_ntz project and flash the binary by clicking on *Run->Run*. Make sure that the board is plugged in.

 ## Running the demo

 With the firmware flashed to the board, open a command prompt, and navigate to the root of the project(lab-iot-reference-stm32u5). Type:

 `python tools/provision.py -i -v `

To know more about the above command, visit the Troubleshooting section at the end of the document.

The script will prompt you to enter the following details. You only need to update wifi_ssid, wifi_credential, mqtt_endpoint and thing name.

```
tls_verify_ca[]: <Click enter>
time_hwm[]: <Click enter>
Wifi_credential[]: <your wifi password>
wifi_auth[]: <Click enter>
mqtt_endpoint[]: <a1qwhobjt*****-ats.iot.us-east-2.amazonaws.com>
wifi_ssid[]: <your wifi ssid>
tls_verify[]: <Click enter>
mqtt_port[]: <Click enter>
thing_name[]: <stm32u5>
```
<img width="500" alt="20" src="https://user-images.githubusercontent.com/44592967/153658546-8c9a7212-86c9-4b38-aeb8-982aeaade8f0.PNG">

The script will do all the certificate and key provisioning by itself. It will also create a thing with the thing name you entered in the terminal, query your aws account for the correct mqtt endpoint, communicate with AWS, download the certificate and key, and save them to your device.

The end of the script will look somewhat like this:

<img width="500" alt="21" src="https://user-images.githubusercontent.com/44592967/153658687-c6bfc826-5653-483c-97b9-957bef57b53a.PNG">

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

The project shows an IoT reference implementation of how to integrate FreeRTOS libraries on STM32U5 platform to perform OTA update with AWS IoT using the *non trustzone* hardware capablities. The demo runs FreeRTOS OTA agent as one of the RTOS tasks in background, which waits for OTA updates from cloud. The non-trustzone version of the demo leverages internal flash memory's dual bank. The total 2MB internal flash is split into two banks of 1MB each. The main firmware is running on one bank, while downloading new firmware image into another bank. Thus, the total firmware image size should not exceed 1MB.

### Provision Code Signing credentials

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

3.Create an ECDSA code-signing certificate:

```
openssl req -new -x509 -config cert_config.txt -extensions my_exts -nodes -days 365 -key ecdsasigner-priv-key.pem -out ecdsasigner.crt

```

4. Import the code-signing certificate and private key into AWS Certificate Manager:

NOTE: This command displays an ARN for your certificate. You need this ARN when you create an OTA update job later

```
aws acm import-certificate --certificate fileb://ecdsasigner.crt --private-key fileb://ecdsasigner.key
```

5. Connect the device to a terminal over serial port. On the command line prompt type following command to provision public key to device:

  `> pki import cert ota_signer_pub`

   Press `Enter` then paste the contents of the PEM public key file `ecdsasigner-pub-key.pem` into the terminal.
   Press `Enter` again.
   Device should successfully provision the public key used to verify the digital signature.

`ota_signer_pub` is the label used to refer to the code signing key during verification of the firmware update.

6. Create a signing profile in AWS to sign the firmware image.

```
aws signer put-signing-profile --profile-name <your profile name> --signing-material certificateArn=<certificate arn created in step 4> --platform AmazonFreeRTOS-Default --signing-parameters certname=ota_signer_pub
```

## Setup OTA S3 bucket, Service role and policies in AWS

1. S3 bucket is used to store the new firmware image to be updated. To create a new S3 bucket follow these steps here: https://docs.aws.amazon.com/freertos/latest/userguide/dg-ota-bucket.html

2. Create a service role which grants permission for OTA service to access the firmware image: https://docs.aws.amazon.com/freertos/latest/userguide/create-service-role.html

3. Create an OTA update policy using the documentatio here: https://docs.aws.amazon.com/freertos/latest/userguide/create-ota-user-policy.html

4. Add a policy for AWS IoT to access the code signing profile: https://docs.aws.amazon.com/freertos/latest/userguide/code-sign-policy.html

## Create a code signed firmware update job

1. Bump up the version of the new firmware image to be updated. From the demo project, open File B-U585I-IOT02A/tz_disabled/Inc/ota_config.h and set APP_VERSION_MAJOR to 1 higher than current version. Build the firmware image using STM32Cube IDE.

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
         "fileType": 0,
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
>>>>>>> cfc299a (Updating Getting Started guide)
> AF>    16351 [OTAAgent] Current State=[WaitingForFileBlock], Event=[RequestFileBlock], New state=[WaitingForFileBlock] (ota.c:2834)
> AF>    15293 [OTAAgent] Extracted parameter: [key: value]=[execution.jobDocument.afr_ota.streamname: AFR_OTA-eb53bc47-6918-4b2c-9c85-a4c74c44a04c] (ota.c:1642)
<INF>    15294 [OTAAgent] Extracted parameter: [key: value]=[execution.jobDocument.afr_ota.protocols: ["MQTT"]] (ota.c:1642)
<INF>    15296 [OTAAgent] Extracted parameter: [key: value]=[filepath: tz_disabled.bin] (ota.c:1642)
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
2. Once all image is downloaded, it verifies signature and activates the new image in the other bank.

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

## Troubleshooting

1. Upon opening STM32CubeIDE, if the prompt to set the directory doesn’t come up, Click on File->Switch Workspace->Other, and set the workspace as shown below:

<img width="314" alt="25" src="https://user-images.githubusercontent.com/44592967/153659709-64d8cc21-9c5e-4fe5-8bf0-d43116662f20.PNG">

2 . For the command to run the script as mentioned in the document:

`
python tools/provision.py -i –v 
`

Note that here are the additional arguments for reference:
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

