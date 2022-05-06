### Key-Value Store
The key-value store located in this folder is used to store runtime configuration values in non-volatile flash memory.
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

Additional runtime configuration keys can be added in the [Common/config/kvstore_config.h](Common/config/kvstore_config.h) file.
