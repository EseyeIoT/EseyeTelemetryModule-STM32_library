#ifndef ETM_INTF_H
#define ETM_INTF_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "stm32l475e_iot01_hsensor.h"
#include "stm32l475e_iot01_psensor.h"
#include "stm32l475e_iot01_tsensor.h"

#define MAJOR_VERSION 0
#define MINOR_VERSION 86

int ETM_HwStatus(void);
void ETM_HwStatusInit(void);
void ETM_HwCheckPowerDown(void);
void ETM_HwPowerDown(void);
void ETM_HwPowerUp(void);
void ETM_Run(void);

#endif
