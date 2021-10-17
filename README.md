# STM32U5 Golden Reference Integration


### Perfroming Firmware Over The Air Update for B-U585I-IOT02A discovery kit

The project shows reference implementation of how to perform firmware over the air update with AWS IoT using both trustzone and non-trustzone hardware capablities. The demo runs FreeRTOS OTA agent library as one of the RTOS tasks in background, which waits for OTA updates from cloud. The non-trustzone version of the demo leverage's board's dual bank flash memory to perform live update on other bank, while the firmare is running on one bank. The total internal flash size of 2MB is split into two banks each of size 1MB, so user should be aware that the total firmware size should not exceed 1MB.

Here is a short walkthrough of how to use the demo to perform over the air update of a new firmware image.

#### Pre requestes

* AWS CLI
* * The steps below uses AWS CLI commands to perform interactions with cloud,
* * AWS CLI can be installed using following link: https://docs.aws.amazon.com/cli/latest/userguide/cli-chap-install.html
* * Run `aws configure` and set the access Key ID, secret Key and region for your AWS account

* If not created already, provision a new thing name and have the required certificates and policies attached to connect to AWS IoT broker.

* OpenSSL command Line tool (latest version)

#### Provisoning Code Signing credentials

Devices uses digital signatures to verify the authenticity of the firmware updates sent over the air. Images are signed by an authorized source who creats the image, and device can verify the signature of the image, using the corresponding public key of the source. Steps below shows how to setup and provision the code signing credentials so as to enable cloud to digitally sign the image and the device to verify the image signature before boot.


1. In your working directory, use the following text to create a file named `cert_config.txt`. Replace `test_signer@amazon.com` with your email address:

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

2. Create the public key from the private key:

```
openssl ec -in ecdsasigner-priv-key.pem  -outform PEM -out ecdsasigner-pub-key.pem
```

3. Create an ECDSA code-signing certificate:

```
openssl req -new -x509 -config cert_config.txt -extensions my_exts -nodes -days 365 -key ecdsasigner-priv-key.pem -out ecdsasigner.crt
```

4. Import the code-signing certificate and private key into AWS Certificate Manager:

** NOTE:  This command displays an ARN for your certificate. You need this ARN when you create an OTA update job later **

```
aws acm import-certificate --certificate fileb://ecdsasigner.crt --private-key fileb://ecdsasigner.key
```

5. Connect the device to a terminal over serial port. On the command line prompt type following command to provision public key to device:
  
  `> pki import cert ota_signer_pub`
   
   Press `Enter` then paste the contents of the PEM public key file `ecdsasigner-pub-key.pem` into the terminal.
   Press `Enter` again.
   Devie should successfully provision the public key used to verify the digital signature.

`ota_signer_pub` is the label used to refer to the code signing key during verification of the firmware update.

6. Create a signing profile in AWS to sign the firmware image

```
aws signer put-signing-profile --profile-name <your profile name> --signing-material certificateArn=<certificate arn created in step 4> --platform AmazonFreeRTOS-Default --signing-parameters certname=ota_signer_pub
```

#### Setup OTA S3 bucket, Service role and policies in AWS

1. S3 bucket is used to store the new firmware image to be updated. To create a new S3 bucket follow these steps here: https://docs.aws.amazon.com/freertos/latest/userguide/dg-ota-bucket.html

2. Create a service role which grants permission for OTA service to access the firmware image: https://docs.aws.amazon.com/freertos/latest/userguide/create-service-role.html

3. Create an OTA update policy using the documentatio here: https://docs.aws.amazon.com/freertos/latest/userguide/create-ota-user-policy.html

4. Add a policy for AWS IoT to access the code signing profile: https://docs.aws.amazon.com/freertos/latest/userguide/code-sign-policy.html


#### Creating a code signed firmware update job

1. Bump up the version of the new firmware image to be updated. From the demo project, open File `B-U585I-IOT02A/tz_disabled/Inc/ota_config.h` and set `APP_VERSION_MAJOR`  to 1 higher than current version. Build the firmware image using STM32Cube IDE.


2. Upload the new image to the s3 bucket created in the previous section.

```
aws s3 cp <image binary path> s3://<s3 bucket for image>/
```
Get the latest s3 file version of the binary image by executing the command below:

```
aws s3api  list-object-versions --bucket <s3 bucket for image > --prefix <image binary name>
```

3. Create a new OTA Update job configuration json file (Example: ota-update-job-config.json) in your filesystem as below. Substitue the parameters with the output obtained from steps above.
```
{
     "otaUpdateId": "<A unique job ID for the OTA job>",
     "targets": [
         "arn:aws:iot:<region>:<accout id>:thing/<thing name>"
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
aws iot create-ota-update --cli-input-json file:///<ota job configuration file path in your filesystem>
```

The command on success returns the OTA Job ID and status of the Job as `CREATE_PENDING`. To get the job ID of the OTA Job, execute the following command and look for `awsIotJobId` field in json document returned. 

```
aws iot get-ota-update --ota-update-id=<ota update id created above>
```
Note down the job ID to check the status of the job later.


#### Monitoring and Verification of  firmware update

1. Once the job is created on the terminal logs, you will see that OTA job is accepted and device starts downloading image.

```
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

## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more information.

## License

This library is licensed under the MIT-0 License. See the LICENSE file.

