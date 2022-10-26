#!python
#
#  FreeRTOS STM32 Reference Integration
#
#  Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy of
#  this software and associated documentation files (the "Software"), to deal in
#  the Software without restriction, including without limitation the rights to
#  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
#  the Software, and to permit persons to whom the Software is furnished to do so,
#  subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all
#  copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
#  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
#  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
#  https://www.FreeRTOS.org
#  https://github.com/FreeRTOS
#
#
import argparse
import fileinput


def process_args():
    parser = argparse.ArgumentParser(argument_default=argparse.SUPPRESS)

    # The path of test_param_config.h.
    parser.add_argument("-p", "--paramPath", type=str)

    # The path of project_defs.mk.
    parser.add_argument("-d", "--projectDefsMkPath", type=str)

    return parser.parse_args()


def main():
    args = process_args()

    print("Start to parse firmware version from test_param_config.h")

    # Users can set the path of test_param_config.h
    if "paramPath" in args:
        paramFilePath = args.paramPath
    else:
        paramFilePath = "./Common/config/test_param_config.h"

    # Users can set the path of tfm.mk
    if "projectDefsMkPath" in args:
        projectDefsMkFilePath = args.projectDefsMkPath
    else:
        projectDefsMkFilePath = "./Projects/b_u585i_iot02a_tfm/project_defs.mk"

    print("paramFilePath: " + paramFilePath)
    print("projectDefsMkFilePath: " + projectDefsMkFilePath)

    # Using readlines()
    paramFile = open(paramFilePath, "r")
    Lines = paramFile.readlines()

    # Read major/minor and build version from test_param_config.h
    for line in Lines:
        if "OTA_APP_VERSION_MAJOR" in line:
            line = line.strip()
            if not line.startswith("*") and not line.startswith("/*"):
                versionMajor = ""
                for c in line:
                    if c.isdigit():
                        versionMajor += c
                # print( "Useful major version line: " + line )
        elif "OTA_APP_VERSION_MINOR" in line:
            line = line.strip()
            if not line.startswith("*") and not line.startswith("/*"):
                versionMinor = ""
                for c in line:
                    if c.isdigit():
                        versionMinor += c
                # print( "Useful minor version line: " + line )
        elif "OTA_APP_VERSION_BUILD" in line:
            line = line.strip()
            if not line.startswith("*") and not line.startswith("/*"):
                versionBuild = ""
                for c in line:
                    if c.isdigit():
                        versionBuild += c
                # print( "Useful build version line: " + line )

    print("Major version: " + versionMajor)
    print("Minor version: " + versionMinor)
    print("Build version: " + versionBuild)

    # Write the config to tfm.mk
    for line in fileinput.input(projectDefsMkFilePath, inplace=True):
        line = line.strip("\n")
        if "NSPE_VERSION = " in line:
            print(
                'NSPE_VERSION = "'
                + versionMajor
                + "."
                + versionMinor
                + "."
                + versionBuild
                + '"'
            )
        else:
            print(line)

if __name__ == "__main__":
    main()
