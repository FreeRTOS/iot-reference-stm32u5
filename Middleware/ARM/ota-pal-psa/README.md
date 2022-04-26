# What is this project

This project ports the FreeRTOS OTA PAL based on PSA API.

PSA is Platform Security Architecture which is initiated by Arm. Please get the details from this [link](https://www.arm.com/why-arm/architecture/platform-security-architecture).

In general, this porting maps the FreeRTOS OTA PAL APIs to PSA Firmware Update and Crypto APIs. It follows the PSA Firmware Update API 0.7 and PSA Cryptography API v1.0 beta3. The process of image write, image verification and image activation are protected by the PSA secure service.

# License

Please check the License.md file in this project root folder for the detailed license information.

Unless stated otherwise, the software is provided under the [MIT License](https://spdx.org/licenses/MIT.html).

# Integration guide

## Integrate PSA Based OTA PAL with the FreeRTOS project

- Use the `libraries/abstractions/ota_pal_psa/ota_pal.c` as the implementation of APIs defined in `vendors/vendor/boards/board/ports/ota_pal_for_aws/ota_pal.h`
- Add the source file `libraries/abstractions/ota_pal_psa/version/application_version.c` to the project.
- `xOTACodeVerifyKeyHandle` is the key handle which indicates the image verification key in PSA Crypto Service. It is used when verifying the image in `otaPal_CloseFile`.
- Build the PSA implementation as the secure side image (check the Trusted Firmware-M example in the following section).
- Integrate the FreeRTOS project with the interface files of the PSA implementation (check the TF-M example below).
- Build the FreeRTOS project which runs in the non-secure world.
- Follow the platform-specific instructions to sign/combine the FreeRTOS image and secure side image.

## Integrate FreeRTOS project with Trusted Firmware-M (TF-M)

[TF-M](https://git.trustedfirmware.org/TF-M/trusted-firmware-m.git/) is a PSA implementation. It implements the PSA Firmware Framework API and developer API such as Secure Storage, Cryptography, Initial Attestation, etc. Refer to [PSA website](https://developer.arm.com/architectures/security-architectures/platform-security-architecture) for more details.

This version of FreeRTOS OTA PAL is supported by TF-M [v1.4.0](https://git.trustedfirmware.org/TF-M/trusted-firmware-m.git/tag/?h=TF-Mv1.4.0) & [v1.5.0](https://git.trustedfirmware.org/TF-M/trusted-firmware-m.git/tag/?h=TF-Mv1.5.0).

Please follow the [Build instructions](https://tf-m-user-guide.trustedfirmware.org/docs/technical_references/instructions/tfm_build_instruction.html) of TF-M to build the secure side image for your platform.

Please check [Integration guide](https://tf-m-user-guide.trustedfirmware.org/docs/integration_guide/tfm_integration_guide.html) of TF-M for integrating the FreeRTOS project with TF-M.
