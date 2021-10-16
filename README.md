## STM32U5 Golden Reference Integration



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

#### Creating new credetials for Code Signing

Digital code signature for firmware images are useful in validtating the authenticity of the source which created the firmware image. Step below shows how to create ECDSA credentails and upload them to cloud, so that it can be used by a valid authority to sign the firmware image at the time of creating an OTA update job. ( Users also have the option of skipping this step and having the image signed and verified separately, however thats not shown in demo).


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
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -pkeyopt ec_param_enc:named_curve -outform PEM -out ecdsasigner.key
```

3. Create an ECDSA code-signing certificate:

```
openssl req -new -x509 -config cert_config.txt -extensions my_exts -nodes -days 365 -key ecdsasigner.key -out ecdsasigner.crt
```

4. Import the code-signing certificate, private key, and certificate chain into AWS Certificate Manager:

```
aws acm import-certificate --certificate fileb://ecdsasigner.crt --private-key fileb://ecdsasigner.key
```

5. This command displays an ARN for your certificate. You need this ARN when you create an OTA update job.

#### Provision the Code signing certificate onto device

1. Flash the current firmware image on the device and connect to the serial port using a serial port terminal of your choice. The terminal should how the command prompt `>` along with the logs.

2. On the command prompt, enter the command `pki import cert ota_signer_pub`

3. Press `Enter` then paste the contents of the PEM certificate file previously created:
 `ecdsasigner.crt`

 4. Press `Enter` twice

 5. The comamnd should run successfully and the code signing certificate should now be provisioned in the device. 


#### Setting up S3 bucket, Service role and policies

1. We need to setup an S3 bucket which hosts the new firmware image to be updated. To create S3 bucket follow these steps here: https://docs.aws.amazon.com/freertos/latest/userguide/dg-ota-bucket.html

2. To create the service role which grants permission for OTA service to access the firmware image: https://docs.aws.amazon.com/freertos/latest/userguide/create-service-role.html

3. Create an OTA update policy as mentioned in the doc: https://docs.aws.amazon.com/freertos/latest/userguide/create-ota-user-policy.html

4. Add a policy for AWS IoT to access the code signing : https://docs.aws.amazon.com/freertos/latest/userguide/code-sign-policy.html


#### Creating a code signed firmware update job

1. Prepare the new firmware image for update. Open File `B-U585I-IOT02A/tz_disabled/Src/ota/ota_update_task.c` and bump up the version of the image by setting `APP_VERSION_MAJOR`  to 1. Build the image.

2. Upload the new image to the s3 bucket created in the previous setup.

3. Create a signing Sign the firmware image using the codesigning credentials:

```
aws signer put-signing-profile --profile-name <your_profile_name> --signing-material certificateArn=<certificate arn created in previous step> --platform stm32u5 --signing-parameters certname=ota_signer_pub
```
```
aws signer start-signing-job --source 's3={bucketName=<s3 bucket for image>,key=<image name> ,version=<latest version id of the image>}' --destination 's3={bucketName=<s3 bucket for image>}' --profile-name <your_signinig_profile_name>
```
The command displays a job ARN and job ID. You need these values next step

4. Create a stream for firmware update


5. Create an OTA Update Job



#### Verification of the firmware updated


## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more information.

## License

This library is licensed under the MIT-0 License. See the LICENSE file.

