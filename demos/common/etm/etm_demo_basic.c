/*
 * Amazon FreeRTOS Eseye ETM MQTT basic demo
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
 * @file etm_demo_basic.c
 * @brief Basic ETM demo.
 *
 * Basic demo of ETM support code.
 * This uses the ETM to connect to AWSIoT via MQTT and publishes an incrementing count
 * to status/<thingname> at specific intervals. It subscribes to update/<thingname> and
 * expects to receive ascii text numeric messages (e.g. '30') which change the interval
 * of the status publishes.
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

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "message_buffer.h"

/* Demo includes. */
#include "etm_basic_config.h"

/* Application includes */
#include "etm_demo_basic.h"

#include "etm_intf.h"

/* Library includes */
#include "etm/etm.h"

/* Reference to the ETM context created in etm_intf.c */
extern ETMObject_t ETMC2cObj;

static uint32_t updatetime = 10000;      /* Periodic update time in mS (default 10000mS) */
static int updatesubidx = -1;
static int statuspubidx = -1;
static int lastcount = 1;

/* Callback function for the 'update' topic to which we are subscribed */
static void updatecb(uint8_t *data, uint32_t length){
	updatetime = strtol((char *)data, NULL, 10);
	updatetime *= 1000;
	configPRINTF(("Poll update %d mS\r\n", updatetime));
}

/* Publish an incrementing count to the 'status' topic */
static void publish(void){
    char msg[20];
    sprintf(msg, "Count %d", lastcount++);
    ETMpublish(&ETMC2cObj, statuspubidx, 1, (uint8_t *)msg, strlen(msg));
};

void stateupd(void){
	configPRINTF(("New state is %d\r\n", ETMC2cObj.currentstate));
}

static void ETMBasicTask( void * pvParameters ){
	/* Holder for the current tick count during timing loop */
    uint32_t tickstart;
    bool toggle_power = true;

    while(1){

    	if(toggle_power == true){
            /* Power up the ETM */
            ETM_HwPowerUp();
            toggle_power = false;
    	}

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
        while((ETMC2cObj.urcseen & ETM_REBOOT_REQUIRED) == 0 && (ETMC2cObj.urcseen & ETM_REBOOT) == 0){
    	    tickstart = ETMC2cObj.GetTickCb();
    	    while((ETMC2cObj.GetTickCb() - tickstart) < pdMS_TO_TICKS(updatetime)){
    	        ETMpoll(&ETMC2cObj);
    	    }
    	    configPRINTF(("Publish\r\n"));
            publish();
        }
        if(ETMC2cObj.urcseen & ETM_REBOOT_REQUIRED){
        	toggle_power = true;
        }else{
        	configPRINTF(("ETM is rebooting...\r\n"));
        }
        if(toggle_power == true){
            configPRINTF(("Restarting ETM...\r\n"));
            /* Reboot required */
            ETM_HwCheckPowerDown();
        }
    }

}

void vStartETMBasicDemo( void ){
    configPRINTF( ( "Creating ETM Demo Task...\r\n" ) );

    /* Create the ETM_demo task */
    ( void ) xTaskCreate( ETMBasicTask,                        /* The function that implements the demo task. */
                          "ETMBasic",                          /* The name to assign to the task being created. */
						  democonfigETM_BASIC_TASK_STACK_SIZE, /* The size, in WORDS (not bytes), of the stack to allocate for the task being created. */
                          NULL,                                /* The task parameter is not being used. */
						  democonfigETM_BASIC_TASK_PRIORITY,   /* The priority at which the task being created will run. */
                          NULL );                              /* Not storing the task's handle. */
}
/*-----------------------------------------------------------*/
