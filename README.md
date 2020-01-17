# STM32 library for BG96 Eseye Telemetry Module

Simple Amazon FreeRTOS library to enable use of the BG96-hosted Eseye Telemetry Module

## What is ETM?

Eseye Telemetry Module is an application that runs on a modem module enabling secure connection to cloud-based IoT services. This works in conjunction with Eseye Anynet Secure SIMs to provide secure end-to-end communication without the need to pre-configure security credentials on the device. All security credentials are sent to the SIM and used to establish a cloud connection without any user intervention. Applications using ETM make use of modem AT commands to publish and subscribe to topics on the cloud service.

## Purpose of this library

To provide a simple API to the ETM AT commands

## Prerequisites

### You will need
Quectel BG96 modem installed with ETM software from Eseye  
Provisioned Eseye Anynet Secure SIM  

### For the examples
Anynet Secure SIM provisioned to a thing on AWSIoT  
STM32 Discovery Board  
Arduino UNO click shield  
MikroElektronika LTE IoT 2 click (BG96 modem) in slot 1 of the shield  

## Images
Kit of parts to build demo_basic example
![Kit of parts](/images/ETM_STM32_Discovery.jpg)

## Examples included
Three examples are included:
### etm_basic_demo
to build this demo ensure ETM_BASIC_DEMO is defined in project properties->C/C++ Build->Settings->MCU GCC Compiler->Preprocessor This demo subscribes to update/<thingname> and publishes to status/<thingname>. Periodically (default 10 seconds) an incrementing count is published. If a numeric value is published to the update topic this is used as a value in seconds for the new update period.

### etm_ota_demo
to build this demo ensure ETM_OTA_DEMO is defined in project properties->C/C++ Build->Settings->MCU GCC Compiler->Preprocessor
This demo subscribes to update/<thingname>. A URL can be published to this topic which refers to a new STM32 binary file. This file will be downloaded by the ETM and applied to the STM32 as a new firmware image.

### etm_awsota_demo
to build this demo ensure ETM_AWSOTA_DEMO is defined in project properties->C/C++ Build->Settings->MCU GCC Compiler->Preprocessor
This demo builds upon etm_basic_demo and adds support for ota updates using AWSIoT jobs.
Ensure you have an S3 bucket on the same AWS account as your IoT 'thing'. 
Create a job document (e.g. fwupdate.json) on your account S3 bucket containing something like:
{
 "operation":"host_firmware",
 "location": "${aws:iot:s3-presigned-url:https://s3.amazonaws.com/mybucket/hfw.bin}"
}
You need to take a copy of an STM binary (use the binary produced from building this application) and drop it into your S3 bucket. The path (e.g. mybucket/hfw.bin from the example above) should match that in the job document. Pre-signed URLs (as above) are supported.
Create a job for your 'thing' using your job document and ETM will download the binary file. etm_awsota_demo periodically polls ETM to see if new software is available. When new software arrives it is copied across to the STM and replaces the executable image.

