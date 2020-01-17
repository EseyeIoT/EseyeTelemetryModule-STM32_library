/*
 * Amazon FreeRTOS Eseye ETM MQTT OTA demo
 * Copyright (C) 2019 Eseye Design Limited.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 *
 * http://www.eseye.com/solutions/anynet-secure/
 */


/**
 * @file etm_demo_ota.c
 * @brief ETM OTA demo.
 *
 * Demo of ETM OTA code.
 * This uses the ETM to connect to AWSIoT via MQTT and subscibes to 'update'.
 * On receipt of a publish to update the message is used as a URL to download
 * an update for the software on this STM32 device
 *
 * Required hardware:
 * STM32 Discovery board
 * Quectel BG96 Modem running ETM application from Eseye
 * Anynet Secure SIM provisioned with a 'thing' for AWSIoT
 */

/* Standard includes. */
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "stdbool.h"

#include "stm32l4xx_hal.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "message_buffer.h"

/* Demo includes. */
#include "etm_basic_config.h"

/* Application includes */
#include "etm_demo_ota.h"

/* Library includes */
#include "etm/etm.h"
#include "etm_intf.h"

/* Reference to the ETM context created in etm_intf.c */
extern ETMObject_t ETMC2cObj;

static int updatesubidx = -1;
static int statuspubidx = -1;
static char *updateurl = NULL;

/* Callback function for the 'update' topic to which we are subscribed */
static void updatecb(uint8_t *data, uint32_t length){
	updateurl = pvPortMalloc(length + 1);
	if(updateurl != NULL){
	    snprintf(updateurl, length + 1, "%s", (char *)data);
	    configPRINTF(("Update URL %s\r\n", updateurl));
	}
}

enum firmware_update_state{OTA_WAITING = 0, OTA_AVAILABLE, OTA_UNAVAILABLE};
static enum firmware_update_state get_firmware = OTA_WAITING;

static void firmware_check_complete(bool available){
	configPRINTF(("ETM responded firmware %savailable\r\n", available == true ? "" : "un"));
	if(available == true)
	    get_firmware = OTA_AVAILABLE;
	else
		get_firmware = OTA_UNAVAILABLE;
}

static void reportBootBank(void){
	FLASH_OBProgramInitTypeDef OBInit;
	HAL_FLASH_Unlock();
    /* Clear OPTVERR bit set on virgin samples */
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);
	/* Allow Access to option bytes sector */
	HAL_FLASH_OB_Unlock();
	/* Get the Dual boot configuration status */
	HAL_FLASHEx_OBGetConfig(&OBInit);
	int flash_boot_bank = ((OBInit.USERConfig & OB_BFB2_ENABLE) == OB_BFB2_ENABLE) ? FLASH_BANK_2 : FLASH_BANK_1;
	configPRINTF(("Flash boot bank set to %d\r\n", flash_boot_bank));
}

static void swapBootBank(void) {
	FLASH_OBProgramInitTypeDef OBInit;

	configPRINTF(("Swapping boot image bank\r\n"));
	vTaskDelay(1000);
	HAL_FLASH_Unlock();

	/* Clear OPTVERR bit set on virgin samples */
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);

	/* Allow Access to option bytes sector */
	HAL_FLASH_OB_Unlock();

	/* Get the Dual boot configuration status */
	HAL_FLASHEx_OBGetConfig(&OBInit);

	/* Enable/Disable dual boot feature */
	OBInit.OptionType = OPTIONBYTE_USER;
	OBInit.USERType   = OB_USER_BFB2;

	if (((OBInit.USERConfig) & (OB_BFB2_ENABLE)) == OB_BFB2_ENABLE){
		OBInit.USERConfig = OB_BFB2_DISABLE;
		configPRINTF(("Enable boot bank 1\r\n"));
	} else {
        OBInit.USERConfig = OB_BFB2_ENABLE;
        configPRINTF(("Enable boot bank 2\r\n"));
	}

	if(HAL_FLASHEx_OBProgram (&OBInit) == HAL_OK){
		/* Start the Option Bytes programming process */
		if (HAL_FLASH_OB_Launch() != HAL_OK){
			configPRINTF(("OB_Launch failed\r\n"));
		}
	} else {
		configPRINTF(("OBProgram failed\r\n"));
	}
}

void stateupd(void){
	configPRINTF(("New state is %d\r\n", ETMC2cObj.currentstate));
}

#define MAXCHUNKSIZE 50 /* Make sure this is an even number */

static void ETMOtaTask( void * pvParameters ){
	/* Holder for the current tick count during timing loop */
    uint32_t tickstart;

    /* Power up the ETM */
    ETM_HwPowerUp();

    reportBootBank();

    /* Initialise the ETM context */
	ETM_Init(&ETMC2cObj, NULL);

	/* Tell ETM to start MQTT */
	ETMstartproto(&ETMC2cObj, ETM_MQTT);

	/* Wait up to 5 seconds for the MQTT subsystem to be ready
	 * This DOES NOT depend on connectivity it just indicates that ETM is ready to start receiving sub/pub
	 * commands and publish messages will be stored to non-volatile memory to be forwarded when connected */
	tickstart = ETMC2cObj.GetTickCb();
	while((ETMC2cObj.GetTickCb() - tickstart) < pdMS_TO_TICKS(5000) && !(ETMC2cObj.urcseen & ETM_MQTTREADY_URC)){
	    ETMpoll(&ETMC2cObj);
	}
	if(!(ETMC2cObj.urcseen & ETM_MQTTREADY_URC)){
		configPRINTF(("Error MQTT not ready\r\n"));
	}

	ETMstatecb(&ETMC2cObj, stateupd);
	ETMupdateState(&ETMC2cObj, ETM_STATE_ON);

	/* Subscribe to update/<thingname> topic */
	updatesubidx = ETMsubscribe(&ETMC2cObj, (char *)"update", updatecb);

	/* Register publish topic as status/<thingname> */
	statuspubidx = ETMpubreg(&ETMC2cObj, (char *)"status");

	/* Main loop which handles the update timer and publishing status */
    while(1){
    	configPRINTF(("Waiting...\r\n"));
    	tickstart = ETMC2cObj.GetTickCb();
    	while((ETMC2cObj.GetTickCb() - tickstart) < pdMS_TO_TICKS(30000) && updateurl == NULL){
    	    ETMpoll(&ETMC2cObj);
    	}

    	if(updateurl != NULL){
    		break;
    	}

    }

    configPRINTF(("Download now\r\n"));

    if(updateurl != NULL){
    	ETMGetHostFW(&ETMC2cObj, updateurl, firmware_check_complete);
    }

    while(get_firmware == OTA_WAITING)
    	ETMpoll(&ETMC2cObj);

    if(get_firmware == OTA_AVAILABLE){
    	uint16_t cs, calccs = 0, collatecs = 0;
    	uint32_t len;
    	bool error = false;

    	uint32_t address = 0x8080000; /* Other bank of flash */
    	FLASH_EraseInitTypeDef EraseInit;
    	uint32_t PageError = 0;

    	/* Erase the bank we're going to write */
    	EraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    	if (READ_BIT(SYSCFG->MEMRMP, SYSCFG_MEMRMP_FB_MODE) == 0){
    		/* No Bank swap */
    		EraseInit.Banks = FLASH_BANK_2;
    	}else{
    		/* Bank swap */
    		EraseInit.Banks = FLASH_BANK_1;
    	}
    	EraseInit.Page = 0;
    	EraseInit.NbPages = (FLASH_BANK_SIZE / FLASH_PAGE_SIZE);
    	/* Erase the pages */
    	HAL_FLASH_Unlock();
    	if (HAL_FLASHEx_Erase(&EraseInit, &PageError) != HAL_OK) {
    	    /* Failed to erase flash block */



    	}

    	if(ETMGetHostFWDetails(&ETMC2cObj, &len, &cs) == 0){
    	    configPRINTF(("Firmware length is %d, cs is %x\r\n", len, cs));
    	    if(len > 0){
    	    	uint32_t walk = 0, collatebyteidx = 0;
    	    	uint32_t dataword[2];
    	    	uint8_t respbuf[50];

    	    	uint8_t reportpc = 0;

    	    	while(walk < len && error == false){
    	    		uint32_t getsize = len - walk;

    	    		if(getsize > MAXCHUNKSIZE)
    	    			getsize = MAXCHUNKSIZE;
    	    		if(ETMReadHostFW(&ETMC2cObj, walk, getsize, respbuf) == 0){
    	    			uint16_t octetidx = 0;
    	    			uint8_t currpc;
    	    			//configPRINTF(("Got data from %d to %d\r\n", walk, walk + getsize));

    	    			currpc = (len - walk) / (len / 100);
    	    			currpc = 100 - currpc;
    	    			if(currpc - reportpc >= 5){
    	    				reportpc = currpc;
    	    				configPRINTF(("%d%%\r\n", reportpc));
    	    			}

    	    		    walk += getsize;

    	    		    while(octetidx < getsize){
    	    		    	/* collate data into 32-bit words */
    	    		    	dataword[collatebyteidx / 4] >>= 8;
    	    		    	dataword[collatebyteidx / 4] |= (respbuf[octetidx] << 24);
                            /* Collate the 16-bit checksum */
    	    		    	collatecs <<= 8;
    	    		    	collatecs |= respbuf[octetidx];

    	    		    	collatebyteidx++;
    	    		    	octetidx++;
                            /* On even bytes adjust the running checksum */
    	    		    	if(collatebyteidx > 0 && collatebyteidx % 2 == 0){
    	    		    	    calccs ^= collatecs;
    	    		    	    collatecs = 0;
    	    		    	}

        	    		    if(collatebyteidx == 8){
        	    		    	uint64_t datadword;
        	    		    	datadword = dataword[1];
        	    		    	datadword <<= 32;
        	    		    	datadword |= (uint64_t)dataword[0];

    	    		            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, datadword) == HAL_OK) {
    	    		    		    /* Data verify */
    	    		    		    if (*((uint64_t *)address) != datadword) {
    	    		    		        /* Verify failed, bomb out */
    	    		    		    	configPRINTF(("Verify fail @ 0x%x\r\n", address));
    	    		    		        error = true;
    	    		    		        break;
    	    		    	        } else {
    	    		    		        address += 8;
    	    		    		    }
    	    		    	    }else{
    	    		    		    /* Failed to write to flash */

    	    		    	    	error = true;
    	    		    		    break;
    	    		    	    }
    	    		            collatebyteidx = 0;
    	    		            dataword[0] = 0;
    	    		            dataword[1] = 0;
    	    		        }
    	    		    }

    	    		}else{
    	    			configPRINTF(("Error reading data\r\n"));
    	    			error = true;
    	    			break;
    	    		}
    	    	}

    	    	if(collatebyteidx > 0 && error == false){
    	    		uint64_t datadword = dataword[1];
    	    		datadword <<= 32;
    	    		datadword |= (uint64_t)dataword[0];
    	    		/* Check for odd-byte and add to checksum */
    	    		if(collatebyteidx % 2 != 0)
    	    			calccs ^= collatecs;
    	    		/* Program the remaining part-doubleword to flash */
    	    	    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, datadword) == HAL_OK) {
    	    	    	/* Data verify */
    	    	    	if (*((uint64_t *)address) != datadword) {
    	    	    	    /* Verify failed, bomb out */

    	    	    	    error = true;
    	    	    	}
    	    	    }else{
    	    	        /* Failed to write to flash */

    	    	    	error = true;
    	    	    }
    	    	}
    	    	if(error == false && calccs != cs){
    	    		configPRINTF(("Checksum mismatch (supplied 0x%x, calculated 0x%x\r\n", cs, calccs));
    	    		error = true;
    	    	}
    	    }
    	}else{
    		configPRINTF(("Couldn't get firmware details\r\n"));
    	}
    	if(error == false){
    		configPRINTF(("Firmware update successful\r\n"));
    		swapBootBank();
    	}else{
    		configPRINTF(("Failed to read and store OTA image \r\n"));
    	}
    }else{
    	configPRINTF(("OTA firmware unavailable\r\n"));
    }

    while(1)
    	ETMpoll(&ETMC2cObj);

}

void vStartETMOtaDemo( void ){
    configPRINTF( ( "Creating ETM OTA Task...\r\n" ) );

    /* Create the ETM_demo task */
    ( void ) xTaskCreate( ETMOtaTask,                        /* The function that implements the demo task. */
                          "ETMota",                          /* The name to assign to the task being created. */
						  democonfigETM_BASIC_TASK_STACK_SIZE, /* The size, in WORDS (not bytes), of the stack to allocate for the task being created. */
                          NULL,                                /* The task parameter is not being used. */
						  democonfigETM_BASIC_TASK_PRIORITY,   /* The priority at which the task being created will run. */
                          NULL );                              /* Not storing the task's handle. */
}
/*-----------------------------------------------------------*/
