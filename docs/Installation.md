# Install prerequisites packages
Using your favorite package manager, install the following prerequisites:
- Python 3.10 with pip
- CMake
- git
- libusb (optional, may be provided by pip libusb_package package)
- ninja
## Windows
### Scoop Pakcage Manager
With scoop installed, run the following command:
```zsh
scoop install python cmake git ninja
```
## Mac OS
### With Homebrew package manager
```zsh
brew install python cmake git libusb ninja
```
# Install STM32CubeIDE
Download the latest version of STM32CubeIDE from the [STMicroelectronics website](https://www.st.com/en/development-tools/stm32cubeide.html).

At the time of this writing, Version 1.8.0 was the latest release:
- [Windows](https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/c5/4c/16/67/3f/90/45/ae/stm32cubeide_win/files/st-stm32cubeide_1.8.0_11526_20211126_0815_x86_64.exe.zip/jcr:content/translations/en.st-stm32cubeide_1.8.0_11526_20211126_0815_x86_64.exe.zip)
- [Mac OS](https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/da/fb/13/97/c5/54/4b/57/stm32cubeide_mac/files/st-stm32cubeide_1.8.0_11526_20211125_0815_x86_64.dmg.zip/jcr:content/translations/en.st-stm32cubeide_1.8.0_11526_20211125_0815_x86_64.dmg.zip)
- [Linux Debian Bundle](https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/49/6d/3d/62/eb/0d/42/4b/stm32cubeide_deb/files/st-stm32cubeide_1.8.0_11526_20211125_0815_amd64.deb_bundle.sh.zip/jcr:content/translations/en.st-stm32cubeide_1.8.0_11526_20211125_0815_amd64.deb_bundle.sh.zip)
- [Linux RPM Bundle](https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/1b/d6/ed/d0/70/b1/47/1b/stm32cubeide_rpm/files/st-stm32cubeide_1.8.0_11526_20211125_0815_amd64.rpm_bundle.sh.zip/jcr:content/translations/en.st-stm32cubeide_1.8.0_11526_20211125_0815_amd64.rpm_bundle.sh.zip)
- [Linux Generic Bundle](https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/dc/49/8b/46/40/16/4e/20/stm32cubeide_lnx/files/st-stm32cubeide_1.8.0_11526_20211125_0815_amd64.sh.zip/jcr:content/translations/en.st-stm32cubeide_1.8.0_11526_20211125_0815_amd64.sh.zip)

Abridged installation instructions are included below. Please refer to the [STM32CubeIDE Installation guide](https://www.st.com/resource/en/user_manual/um2563-stm32cubeide-installation-guide-stmicroelectronics.pdf) and the included instructions for your platform if addiitonal help is needed.

## Mac OS (Install via terminal)
From a terminal window, you can accomplish installation with the following commands:

Download and unzip the package linked above:
```zsh
curl "https://www.st.com/content/ccc/resource/technical/software/sw_development_suite/group0/da/fb/13/97/c5/54/4b/57/stm32cubeide_mac/files/st-stm32cubeide_1.8.0_11526_20211125_0815_x86_64.dmg.zip/jcr:content/translations/en.st-stm32cubeide_1.8.0_11526_20211125_0815_x86_64.dmg.zip" | bsdtar -xvf -
```

Mount the resulting dmg disk image (auto-accepting the license agreement)
```zsh
yes | PAGER=cat hdiutil attach st-stm32cubeide_1.8.0_11526_20211125_0815_x86_64.dmg
```

Install the stlink-server package if desired
```zsh
osascript -e 'do shell script "installer -pkg /Volumes/STM32CubeIDE/st-stlink-server.2.0.2-3.pkg -target /" with administrator privileges'
```

Copy the STM32CubeIDE application to /Applications (this could take up to a minute)
```zsh
cp -r /Volumes/STM32CubeIDE/STM32CubeIDE.app /Applications/STM32CubeIDE_3_18.app
```

Unmount the disk image and delete it:
```zsh
umount /Volumes/STM32CubeIDE
rm st-stm32cubeide_1.8.0_11526_20211125_0815_x86_64.dmg
```

The STM32CubeIDE MacOS application is not properly signed and notarized, so the following command must be run after copying the .app to /Applications.
```zsh
xattr -c /Applications/STM32CubeIDE_3_18.app
```

Finally, open STM32CubeIDE:
```zsh
open /Applications/STM32CubeIDE_3_18.app
```

# Clone the repository and submodules
Using your favorite unix-like console application, run the following commands to clone and initialize the git respository and it's submodules:
```
git clone https://github.com/FreeRTOS/lab-iot-reference-stm32u5.git
git -C lab-iot-reference-stm32u5 submodule update --init
```

# Setup python environment
The python environment will be setup automatically when building the tfm-enabled project. You can also manually run the setup script using the following instructions:

Navigate to the root of the git repository:
```zsh
cd /path/to/lab-iot-reference-stm32u5
```

Install the virtualenv package
```zsh
pip3 install virtualenv
```

Source the setup script to enter the python virutal environment
```zsh
source tools/env_setup.sh
```

# PyOCD and libusb installation
Connect the target board to your machine and check if pyocd is able to connect successfully using ```pyocd list``` command. Extra steps may be required on Windows and Linux platforms. If everything is setup correctly, you should see the output below:

```
$ pyocd list
  #   Probe                            Unique ID
-----------------------------------------------------------------
  0   B-U585I-IOT02A [stm32u585aiix]   0008002B4741500XXXXXXXXX
```

## "no libusb library was found"

If you receive the following error message:
```
0001021:WARNING:common:STLink, CMSIS-DAPv2 and PicoProbe probes are not supported because no libusb library was found.
```
Try installing libusb via the pip package manager and ```libusb_package``` package.
```zsh
pip3 install libusb_package
```

## Linux permission errors
Some linux distributions may require you to install udev scripts which grant permissions to access usb devices directly.
The directions from the [pyOCD udev documentation](https://github.com/pyocd/pyOCD/tree/main/udev) are summarized below:

If your board is not listed in the output of ```pyocd list```, please follow the steps below to install the necessary udev rules.

Create a temporary directory
```zsh
export TEMPDIR=$(mktemp -d)
```

Download udev rule files
```zsh
curl https://raw.githubusercontent.com/pyocd/pyOCD/main/udev/49-stlinkv2-1.rules > $TEMPDIR/49-stlinkv2-1.rules
curl https://raw.githubusercontent.com/pyocd/pyOCD/main/udev/49-stlinkv2.rules > $TEMPDIR/49-stlinkv2.rules
curl https://raw.githubusercontent.com/pyocd/pyOCD/main/udev/49-stlinkv3.rules > $TEMPDIR/49-stlinkv3.rules
curl https://raw.githubusercontent.com/pyocd/pyOCD/main/udev/50-cmsis-dap.rules > $TEMPDIR/50-cmsis-dap.rules
curl https://raw.githubusercontent.com/pyocd/pyOCD/main/udev/50-picoprobe.rules > $TEMPDIR/50-picoprobe.rules
```

Install the udev rules to the /etc/udev/rules.d/ directory.

Note: this directory may be different for some linux distributions.
```
sudo cp $TEMPDIR/*.rules /etc/udev/rules.d/
```

Tell the udev daemon to check for new rule files and generate events for currently connected devices.
```
sudo udevadm control --reload
sudo udevadm trigger
```

Optionally, remove the temporary directory and it's contents
```
rm -r $TEMPDIR
```

# Importing the projects into STM32CubeIDE
