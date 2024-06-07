# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Users/jungle/esp/v5.1/esp-idf/components/bootloader/subproject"
  "G:/idf_demo/sample_project_weather_led_strip_IOT/build/bootloader"
  "G:/idf_demo/sample_project_weather_led_strip_IOT/build/bootloader-prefix"
  "G:/idf_demo/sample_project_weather_led_strip_IOT/build/bootloader-prefix/tmp"
  "G:/idf_demo/sample_project_weather_led_strip_IOT/build/bootloader-prefix/src/bootloader-stamp"
  "G:/idf_demo/sample_project_weather_led_strip_IOT/build/bootloader-prefix/src"
  "G:/idf_demo/sample_project_weather_led_strip_IOT/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "G:/idf_demo/sample_project_weather_led_strip_IOT/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "G:/idf_demo/sample_project_weather_led_strip_IOT/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
