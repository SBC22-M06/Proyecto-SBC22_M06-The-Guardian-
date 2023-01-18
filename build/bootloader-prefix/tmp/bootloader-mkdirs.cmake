# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Espressif/frameworks/esp-idf-v4.4.3/components/bootloader/subproject"
  "D:/Downloads/tcp/build/bootloader"
  "D:/Downloads/tcp/build/bootloader-prefix"
  "D:/Downloads/tcp/build/bootloader-prefix/tmp"
  "D:/Downloads/tcp/build/bootloader-prefix/src/bootloader-stamp"
  "D:/Downloads/tcp/build/bootloader-prefix/src"
  "D:/Downloads/tcp/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Downloads/tcp/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
