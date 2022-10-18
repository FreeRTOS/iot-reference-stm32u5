SET ST_IDE_PATH=<PATH TO IDE e.g. C:\ST\STM32CubeIDE_x.y.z\STM32CubeIDE>
SET TOOLCHAIN_PATH=<PATH TO TOOLCHAIN e.g. C:\ST\STM32CubeIDE_x.y.z\STM32CubeIDE\lugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_w.x.y.z\tools\bin>
SET BASH_EXE=<PATH TO sh.exe e.g. C:\Program Files\Git\bin\sh.exe>
SET SOURCE_PATH=%1%
SET IDT_CONFIG_PATH=%2%

"%BASH_EXE%" -c "cygpath.exe \"%ST_IDE_PATH%\"" > "tempfilepath.txt"
SET /p CVT_ST_IDE_PATH= < "tempfilepath.txt"
echo "%CVT_ST_IDE_PATH%"

"%BASH_EXE%" -c "cygpath.exe \"%TOOLCHAIN_PATH%\"" > "tempfilepath.txt"
SET /p CVT_TOOLCHAIN_PATH= < "tempfilepath.txt"
echo "%CVT_TOOLCHAIN_PATH%"

"%BASH_EXE%" -c "cygpath.exe \"%SOURCE_PATH%\"" > "tempfilepath.txt"
SET /p CVT_SOURCE_PATH= < "tempfilepath.txt"
echo "%CVT_SOURCE_PATH%"

"%BASH_EXE%" -c "cygpath.exe \"%IDT_CONFIG_PATH%\"" > "tempfilepath.txt"
SET /p CVT_IDT_CONFIG_PATH= < "tempfilepath.txt"
echo "%CVT_IDT_CONFIG_PATH%"
rm "tempfilepath.txt"

call "%BASH_EXE%" -c "%IDT_CONFIG_PATH%/flash_ntz.sh %CVT_SOURCE_PATH% %CVT_ST_IDE_PATH% %CVT_TOOLCHAIN_PATH%"
exit 0