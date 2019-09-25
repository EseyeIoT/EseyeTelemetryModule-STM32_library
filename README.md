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
