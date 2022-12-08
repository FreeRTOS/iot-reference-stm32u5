# Changelog for STM32U5 Featured FreeRTOS IoT Integration

## v202212.00 (December 2022)

### Changes
- [#71](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/71) Update Long Term Support (LTS) libraries to 202210.01-LTS:
  * [FreeRTOS-Kernel V10.5.1](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/V10.5.1/History.txt)
  * [coreMQTT v2.1.1](https://github.com/FreeRTOS/coreMQTT/blob/v2.1.1/CHANGELOG.md)
  * [coreHTTP v3.0.0](https://github.com/FreeRTOS/coreHTTP/tree/v3.0.0)
  * [corePKCS11 v3.5.0](https://github.com/FreeRTOS/corePKCS11/tree/v3.5.0)
  * [coreJSON v3.2.0](https://github.com/FreeRTOS/coreJSON/tree/v3.2.0)
  * [backoffAlgorithm v1.3.0](https://github.com/FreeRTOS/backoffAlgorithm/tree/v1.3.0)
  * [AWS IoT Device Shadow v1.3.0](https://github.com/aws/Device-Shadow-for-AWS-IoT-embedded-sdk/tree/v1.3.0)
  * [AWS IoT Device Defender v1.3.0](https://github.com/aws/Device-Defender-for-AWS-IoT-embedded-sdk/tree/v1.3.0)
  * [AWS IoT Jobs v1.3.0](https://github.com/aws/Jobs-for-AWS-IoT-embedded-sdk/tree/v1.3.0)
  * [AWS IoT Over-the-air Update v3.4.0](https://github.com/aws/ota-for-aws-iot-embedded-sdk/tree/v3.4.0)

- [#79](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/79) FreeRTOS awareness in CubeIDE gdb server config
- [#77](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/77) fix: cli_conf checks argument count to conf set
- [#76](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/76) fix: mbedtls_transport now prints a valid IP
- [#75](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/75) provision.py: Fix aws account credential detection from args
- [#74](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/74) Fix CI build
- [#56](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/56), [#60](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/60) STM32 BSP submodules
- [#59](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/59) env_setup.sh: Install virtualenv package if not already installed
- [#58](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/58) Fix tfm.mk include of project_defs.mk
- [#57](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/57) Fix various build related bugs
- [#55](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/55) Update example config and flash script after further testing
- [#63](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/63), [#64](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/64), [#65](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/65), [#66](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/66), [#67](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/67), [#68](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/68), [#70](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/70) and [#80](https://github.com/FreeRTOS/iot-reference-stm32u5/pull/80) Documentation update

## v202205.00 ( May 2022 )

This is the first release for the repository.

The repository contains IoT Reference integration projects using STM32U585 IoT Discovery Kit. This release includes the following examples:

* A Trusted Firmware-M IoT reference project which integrates AWS IoT demo tasks with Trusted Firmware-M architecture leveraging secure TrustZone capabilities of ARM Cortex M33.
* A Non-TrustZone IoT reference project which operates in a non-trustzone mode.
