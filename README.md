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


#### Provisioning new credetials for Code Signing

Digital code signature for firmware images are useful in validtating the authenticity of the source which created the firmware image. Step below shows how to create ECDSA credentails and upload them to cloud, so that it can be used by a valid authority to sign the firmware image at the time of creating an OTA update job. ( Users also have the option of skipping this step and having the image signed and verified separately).






#### Setting up OTA Service roles and policies


#### Creating a firmware Update JOb


#### Verification of the firmware updated


## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more information.

## License

This library is licensed under the MIT-0 License. See the LICENSE file.

