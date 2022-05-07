### Command Line Interface (CLI)
The CLI interface is used to provision the device. There is a python script to automatically provision the device and register Thing into cloud.
See [ Getting Started Guide ](../../Getting_Started_Guide.md)

### Other Unix-like utilities
The following other utilities are also available in this image:

```
ps
    List the status of all running tasks and related runtime statistics.

kill
    kill [ -SIGNAME ] <Task ID>
        Signal a task with the named signal and the specified task id.

    kill [ -n ] <Task ID>
        Signal a task with the given signal number and the specified task id.

killall
    killall [ -SIGNAME ] <Task Name>
    killall [ -n ] <Task Name>
        Signal a task with a given name with the signal number or signal name given.

heapstat
    heapstat [-b | --byte]
        Display heap statistics in bytes.

    heapstat -h | -k | --kibi
        Display heap statistics in Kibibytes (KiB).

    heapstat -m | --mebi
        Display heap statistics in Mebibytes (MiB).

    heapstat --kilo
        Display heap statistics in Kilobytes (KB).

    heapstat --mega
        Display heap statistics in Megabytes (MB).

reset
    Reset (reboot) the system.

uptime
    Display system uptime.

rngtest <number of bytes>
    Read the specified number of bytes from the rng and output them base64 encoded.

assert
   Cause a failed assertion.
```
