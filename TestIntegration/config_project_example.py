"""
Search for CHANGEME below and replace it with something approprate for your test configuration.
Note comments with REQUIRED are for fields which are required for testing. CHANGEME implied REQUIRED.
"""

from pprint import pformat
from pathlib import Path
from dataclasses import dataclass, asdict

from otaendtoend.helpers.logger import logger
from otaendtoend.helpers.utils import get_dir_otaendtoend, get_git_revision_hash


@dataclass
class OtaProject:
    """
    Representation of a project for OTA
    """

    name: str = "OtaProject"
    ota_platform: str = None
    ota_test_name: str = None
    # Should be either "MQTT" or "HTTP" or "MIXED"
    data_protocol: str = None
    network_type: str = None
    es_enable: bool = False
    fr_repo: str = None
    fr_repo_hash: str = None
    pre_lts_demo: bool = False
    src_log_level: str = "LOG_INFO"

    # AWS configuration
    s3_bucket_name: str = "CHANGEME" # example "otatest001"
    ota_update_role_arn: str = "CHANGEME" # example "arn:aws:iam::270533122096:role/OTATestRole001"
    ecdsa_signer_certificate_arn: str = "CHANGEME" # example "arn:aws:acm:us-west-2:270533122096:certificate/9985e7e0-2cf5-48ee-9768-2dbf6fc7c1ac"
    ecdsa_signer_certificate_public_key: str = \
"CHANGEME"
# exmaple
#"-----BEGIN PUBLIC KEY-----\n"\
#"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE6wEohmFauiuiMLywU75bXi+RV0Q1\n"\
#"1hjyluPeYzgh/PA8JgXnzQGQBDG9z5hUbpa/fVgJFOjXmS5a1+2Pbe9SVA==\n"\
#"-----END PUBLIC KEY-----\n"
    ecdsa_signer_certificate_private_key_sec1: str = \
"CHANGEME"
# example
#"-----BEGIN PRIVATE KEY-----\n"\
#"MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgtvC6v89dM5LauNTQ\n"\
#"DalI06rusWO/j1U8MJklRFhgi/OhRANCAATrASiGYVq6K6IwvLBTvlteL5FXRDXW\n"\
#"GPKW495jOCH88DwmBefNAZAEMb3PmFRulr99WAkU6NeZLlrX7Y9t71JU\n"\
#"-----END PRIVATE KEY-----\n"
    ecdsa_untrusted_signer_certificate_arn: str = "CHANGEME" # example "arn:aws:acm:us-west-2:270533122096:certificate/ee8e9df2-6795-4450-8c9e-b74f96575035"
    rsa_signer_certificate_arn: str = ""  
    rsa_untrusted_signer_certificate_arn: str = ""
    signer_platform: str = "AmazonFreeRTOS-Default"
    signer_certificate_file_name: str = "ota_signer_pub" # REQUIRED
    signer_oid: str = "sig-sha256-ecdsa" # REQUIRED
    compile_codesigner_certificate: bool = True # REQUIRED
    clean_on_exit: bool = True
    ota_timeout_sec: int = 600
    thing_name: str = None
    iotrc_path: str = None

    # ST Specific configuration
    ca_root_certificate: str = "-----BEGIN CERTIFICATE-----\n"\
"MIID7zCCAtegAwIBAgIBADANBgkqhkiG9w0BAQsFADCBmDELMAkGA1UEBhMCVVMx\n"\
"EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxJTAjBgNVBAoT\n"\
"HFN0YXJmaWVsZCBUZWNobm9sb2dpZXMsIEluYy4xOzA5BgNVBAMTMlN0YXJmaWVs\n"\
"ZCBTZXJ2aWNlcyBSb290IENlcnRpZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5\n"\
"MDkwMTAwMDAwMFoXDTM3MTIzMTIzNTk1OVowgZgxCzAJBgNVBAYTAlVTMRAwDgYD\n"\
"VQQIEwdBcml6b25hMRMwEQYDVQQHEwpTY290dHNkYWxlMSUwIwYDVQQKExxTdGFy\n"\
"ZmllbGQgVGVjaG5vbG9naWVzLCBJbmMuMTswOQYDVQQDEzJTdGFyZmllbGQgU2Vy\n"\
"dmljZXMgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkgLSBHMjCCASIwDQYJKoZI\n"\
"hvcNAQEBBQADggEPADCCAQoCggEBANUMOsQq+U7i9b4Zl1+OiFOxHz/Lz58gE20p\n"\
"OsgPfTz3a3Y4Y9k2YKibXlwAgLIvWX/2h/klQ4bnaRtSmpDhcePYLQ1Ob/bISdm2\n"\
"8xpWriu2dBTrz/sm4xq6HZYuajtYlIlHVv8loJNwU4PahHQUw2eeBGg6345AWh1K\n"\
"Ts9DkTvnVtYAcMtS7nt9rjrnvDH5RfbCYM8TWQIrgMw0R9+53pBlbQLPLJGmpufe\n"\
"hRhJfGZOozptqbXuNC66DQO4M99H67FrjSXZm86B0UVGMpZwh94CDklDhbZsc7tk\n"\
"6mFBrMnUVN+HL8cisibMn1lUaJ/8viovxFUcdUBgF4UCVTmLfwUCAwEAAaNCMEAw\n"\
"DwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYEFJxfAN+q\n"\
"AdcwKziIorhtSpzyEZGDMA0GCSqGSIb3DQEBCwUAA4IBAQBLNqaEd2ndOxmfZyMI\n"\
"bw5hyf2E3F/YNoHN2BtBLZ9g3ccaaNnRbobhiCPPE95Dz+I0swSdHynVv/heyNXB\n"\
"ve6SbzJ08pGCL72CQnqtKrcgfU28elUSwhXqvfdqlS5sdJ/PHLTyxQGjhdByPq1z\n"\
"qwubdQxtRbeOlKyWN7Wg0I8VRw7j6IPdj/3vQQF3zCepYoUz8jcI73HPdwbeyBkd\n"\
"iEDPfUYd/x7H4c7/I9vG+o1VTqkC50cRRj70/b17KSa7qWFiNyi2LSr2EIZkyXCn\n"\
"0q23KXB56jzaYyWf/Wi3MOxw+3WKt21gZ7IeyLnp2KhvAotnDU0mV3HaIPzBSlCN\n"\
"sSi6\n"\
"-----END CERTIFICATE-----\n"

    # Source configuration
    src_root: Path = Path("CHANGEME") # example /home/adamds/repos/lab-iot-reference-stm32u5 # no trailing slash
    vendor_board_path: Path = None
    version_major: int = 0
    version_minor: int = 9
    version_build: int = 0
    wifi_password: str = "CHANGEME" # Requirement checked at post_init() for wifi platforms
    wifi_security: str = None # Requirement checked at post_init() for wifi platforms
    wifi_ssid: str = "CHANGEME" # Requirement checked at post_init() for wifi platforms
    windows_network_interface: str = "0" # TODO remove from project and place only in platform
    jlink_sn: str = None
    private_key_location: Path = None
    private_key_path_ecdsa_sec1: Path = None # Requirement checked at post_init() for Renesas platforms

    # Custom-specific configuration, only used when board_name is "custom"
    custom_build_script_location: str = src_root +  "/TestIntegration/build.sh"
    custom_image_location: str = "CHANGEME" # example "/home/adamds/repos/lab-iot-reference-stm32u5/Projects/b_u585i_iot02a_ntz/Debug/b_u585i_iot02a_ntz.bin"
    custom_flash_script_location: str = src_root + "/TestIntegration/flash.sh"
    custom_device_type: str = "device" # "simulator" or "device"

    custom_ota_config_path: Path = Path(src_root + "/Common/config/ota_config.h")
    custom_ota_log2_file_block_size_pattern: str = "#define otaconfigLOG2_FILE_BLOCK_SIZE"
    custom_ota_max_num_blocks_request_pattern: str = "#define otaconfigMAX_NUM_BLOCKS_REQUEST"

    custom_wifi_credential_path: Path = Path(src_root + "/Common/config/kvstore_config.h")
    custom_wifi_ssid_pattern: str = "#define WIFI_SSID_DFLT"
    custom_wifi_password_pattern: str = "#define WIFI_PASSWORD_DFLT"
    custom_wifi_security_pattern: str = "#define WIFI_SECURITY_DFLT"

    custom_mqtt_broker_path: Path = Path(src_root + "/Common/config/kvstore_config.h")
    custom_mqtt_broker_endpoint_pattern: str = "#define MQTT_ENDPOINT_DFLT"
    custom_mqtt_broker_port_pattern: str = "#define MQTT_PORT_DFLT"

    custom_client_credential_path: Path = Path(src_root + "/Common/config/kvstore_config.h")
    custom_client_thing_name_pattern: str = "#define THING_NAME_DFLT"

    custom_client_credential_keys_path: Path = Path(src_root + "/Common/config/ota_config.h")
    custom_client_credential_certificate_pem_pattern: str = "#define keyCLIENT_CERTIFICATE_PEM"
    custom_client_credential_certificate_private_key_pattern: str = "#define keyCLIENT_PRIVATE_KEY_PEM"
    custom_ca_root_certificate_pem_pattern: str = "#define keyCA_ROOT_CERT_PEM"
    custom_client_credential_jitr_authority_pem_pattern: str = None

    custom_application_version_path: Path = Path(src_root + "/Common/config/ota_config.h")
    custom_application_version_major_pattern: str = "#define APP_VERSION_MAJOR"
    custom_application_version_minor_pattern: str = "#define APP_VERSION_MINOR"
    custom_application_version_build_pattern: str = "#define APP_VERSION_BUILD"

    custom_core_mqtt_config_path: Path = Path(src_root + "/Common/config/core_mqtt_config.h")
    custom_core_mqtt_log_pattern: str = "#define LIBRARY_LOG_LEVEL"
    custom_core_mqtt_log_setting: str = "LOG_DEBUG"

    custom_ota_bootloader_public_key_path: Path = None # Requirement for Renesas rx65n-rsk platforms only
    custom_ota_bootloader_cproject_path: Path = None # Requirement for Renesas rx65n-rsk platforms only

    custom_ota_codesigner_certificate_path: Path = Path(src_root + "/Common/config/ota_config.h")
    custom_ota_signing_certificate_pem_pattern: str = "#define otapalconfigCODE_SIGNING_CERTIFICATE"

    custom_ota_task_path: Path = Path(src_root + "/Projects/b_u585i_iot02a_ntz/Src/ota/ota_update_task.c")
    custom_ota_task_name_pattern: str = "static void prvOTAAgentTask("
    custom_ota_task_code_pattern: str = "OTA_EventProcessingTask( pvParam );"
    
    custom_freertos_config_path: Path = Path(src_root + "/Common/config/FreeRTOSConfig.h")
    custom_freertos_config_network_interface_to_use_pattern: str = "#define configNETWORK_INTERFACE_TO_USE"

    # Monitor configuration
    serial_port: str = "CHANGEME" # example "/dev/ttyACM0"
    log_to_console: bool = False
    log_dir: Path = None
    log_file_monitor: Path = None
    log_file_report: Path = None
    serial_run_baudrate: int = 115200
    serial_timeout_sec: int = 30
    run_executable: bool = False
    output: Path = None
    device_firmware_file_name: str = "aws_demos.bin"
    print_monitor: bool = False

    # USB Hub configuration
    usb_ports: tuple = tuple()

    # auxiliary tools
    renesas_secure_flash_tool_path: str = None # Renesas tool, should be able to match the path in afr host tools config

    def __post_init__(self):
        if self.fr_repo_hash == None:
            self.fr_repo_hash = get_git_revision_hash(self.src_root)

        if self.log_dir == None:
            self.log_dir = Path(
                get_dir_otaendtoend(),
                "logs",
                self.ota_platform,
                f"{self.ota_test_name}",
            )

        if self.log_file_monitor == None:
            self.log_file_monitor = Path(self.log_dir, f"monitor_{self.data_protocol}_{self.network_type}.log")

        if self.log_file_report == None:
            self.log_file_report = Path(self.log_dir, f"report_{self.data_protocol}_{self.network_type}.txt")

        if self.network_type == "WIFI":
            if self.wifi_password == None:
                self.wifi_password = ""
            if self.wifi_security == None:
                self.wifi_security = ""
            if self.wifi_ssid == None:
                self.wifi_ssid = "Guest"

        if self.private_key_location == None:
            self.private_key_location = Path(self.src_root, "keys")

        if self.private_key_path_ecdsa_sec1 == None:
            # Note if this path is changed, will also need to reflect the change in AFR Host Tools Config as well
            self.private_key_path_ecdsa_sec1 = Path(self.private_key_location, "ecdsasigner.privatekey")

        if "renesas" in self.ota_platform:
            if self.renesas_secure_flash_tool_path == None:
                self.renesas_secure_flash_tool_path = "'SETUP:renesas_flash_programmer_path' (get from: https://github.com/renesas/Amazon-FreeRTOS-Tools)"

            if self.ecdsa_signer_certificate_public_key == None:
                self.ecdsa_signer_certificate_public_key = "'SETUP:ecdsa_signer_certificate_public_key' required for Renesas"

            if self.ecdsa_signer_certificate_private_key_sec1 == None:
                self.ecdsa_signer_certificate_private_key_sec1 = "'SETUP:ecdsa_signer_certificate_private_key_sec1' required for Renesas"

    def pretty(self):
        return f"OTA Project dataclass:\n{pformat(asdict(self), width=150)}"

    def check_setup(self):
        # TODO [C] may want to phrase it as "REQUIRED" instead of "SETUP"
        if "SETUP" in self.pretty():
            logger.error("Please configure OTA project properly:")
            print(self.pretty())
            logger.error(
                "Fix any 'SETUP' strings by either adding to CLI or config_user.py"
            )
            exit(1)
