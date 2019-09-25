/**
  ******************************************************************************
  * @file    etm.h
  * @author  Paul Tupper @ Eseye
  ******************************************************************************
  */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __ETM_H
#define __ETM_H

#ifdef __cplusplus
 extern "C" {
#endif  

/* Includes ------------------------------------------------------------------*/
#include "stdint.h"
#include "string.h"
#include "stdio.h"
#include "etm_conf.h"


/* Private Constants --------------------------------------------------------*/
#define  RET_NONE           0x0000  /* RET_NONE shall be 0x0: don't change this value! */
#define  RET_CRLF           0x0001
#define  RET_IDLE           0x0002  
#define  RET_OK             0x0004
#define  RET_ERROR          0x0008
#define  RET_EURDY          0x0010  
#define  RET_EMQRDY         0x0020
#define  RET_MQTTREC        0x0040  
#define  RET_SENDFAIL       0x0080
#define  RET_SENDOK         0x0100
#define  RET_SUBOPEN        0x0200
#define  RET_SUBCLOSE       0x0400
#define  RET_PUBOPEN        0x0800
#define  RET_PUBCLOSE       0x1000
#define  RET_APPRDY         0x2000
#define  RET_STATEURC       0x4000
#define  RET_ANY            0x80000000  /* Scan for persistent responses (normally URCs) only */
#define  NUM_RESPONSES      15

#define ETM_TOUT_SHORT                         50  /* 50 ms */
#define ETM_TOUT_300                          350  /* 0,3 sec + margin */
#define ETM_TOUT_ATSYNC                       500   
#define ETM_TOUT_5000                        5500  /* 5 sec + margin */
#define ETM_TOUT_15000                      16500  /* 15 sec + margin */
#define ETM_TOUT_40000                      42000  /* 40 sec + margin */
#define ETM_TOUT_60000                      64000  /* 1 min + margin */
#define ETM_TOUT_75000                      78000  /* 75 sec + margin */
#define ETM_TOUT_150000                    156000  /* 2,5 min + margin */
#define ETM_TOUT_180000                    186000  /* 3 min + margin */

/* Exported macro-------------------------------------------------------------*/
#define MIN(a, b)  ((a) < (b) ? (a) : (b))


/* Exported typedef ----------------------------------------------------------*/   
typedef int8_t (*IO_Init_Func)( void);
typedef int8_t (*IO_DeInit_Func)( void);
typedef int8_t (*IO_Baudrate_Func)(uint32_t BaudRate);
typedef void (*IO_Flush_Func)(void);
typedef int16_t (*IO_Send_Func)( uint8_t *, uint16_t);
typedef int16_t (*IO_ReceiveOne_Func)(uint8_t* pSingleData);
typedef uint32_t (*App_GetTickCb_Func)(void);


typedef struct {
  uint32_t retval;
  char retstr[100];
} ETM_RetKeywords_t;

typedef enum {
  ETM_RETURN_OK             = RET_OK,         /*shall be aligned with above definitions */
  ETM_RETURN_ERROR          = RET_ERROR,      /*shall be aligned with above definitions */
  ETM_RETURN_RETRIEVE_ERROR = -1,
  ETM_RETURN_NO_DATA        = -2,
  ETM_RETURN_SEND_ERROR     = -3
}ETM_Return_t;

typedef enum {
  ETM_INIT_RET_OK        = RET_OK,       /*shall be aligned with above definitions */
  ETM_INIT_RET_AT_ERR    = 0x04,
  ETM_INIT_RET_SIM_ERR   = 0x08,        
  ETM_INIT_RET_IO_ERR    = 0x10,
  ETM_INIT_OTHER_ERR     = 0x20
}ETM_InitRet_t;

typedef struct {
  IO_Init_Func       IO_Init;  
  IO_DeInit_Func     IO_DeInit;
  IO_Baudrate_Func   IO_Baudrate;
  IO_Flush_Func      IO_FlushBuffer;  
  IO_Send_Func       IO_Send;
  IO_ReceiveOne_Func IO_ReceiveOne;  
} ETM_IO_t;

#define MAX_SUB_TOPICS 8
#define MAX_PUB_TOPICS 8

/* Prototype for the AT command response callback function */
typedef void (*_atcb)(char *data);
/* Prototype for the message callback function */	
typedef void (*_msgcb)(uint8_t *data, uint32_t length);
/* Publish topic state */
typedef enum {PUB_TOPIC_ERROR = -1, PUB_TOPIC_NOT_IN_USE = 0, PUB_TOPIC_REGISTERING, PUB_TOPIC_REGISTERED, PUB_TOPIC_UNREGISTERING} tpubTopicState;
/* Subscribe topic state */
typedef enum {SUB_TOPIC_ERROR = -1, SUB_TOPIC_NOT_IN_USE = 0, SUB_TOPIC_SUBSCRIBING, SUB_TOPIC_SUBSCRIBED, SUB_TOPIC_UNSUBSCRIBING} tsubTopicState;
/* Current state of connectivity */
typedef enum {ETM_UNKNOWN = -1, ETM_IDLE = 0, ETM_WAITKEYS, ETM_NETWORKSTART, ETM_SSLSTART, ETM_SSLCONN, ETM_MQTTSTART, ETM_MQTTREADY, ETM_MQTTSUB, ETM_UDPACTIVE, ETM_ERROR} tetmState;
/* Current state callback */
typedef void (*_statecb)(void);
/* Request state type */
typedef enum {ETM_STATE_ONCE = 0, ETM_STATE_ON, ETM_STATE_OFF} tetmRequestState;
typedef enum {ETM_MQTT, ETM_UDP} tetmProto;

#define ETM_READY_URC        (0x01 << 0)
#define ETM_MQTTREADY_URC    (0x01 << 1)
#define ETM_UDPREADY_URC     (0x01 << 2)

/* Subscribed topic array element */	
struct subtpc{
  _msgcb messagecb;
  tsubTopicState substate;
};

/* Publish topic array element */
struct pubtpc{
  tpubTopicState pubstate;
#ifdef TIMEOUT_RESPONSES
  /* Include a senttime for each pub to enable timeout */
  unsigned long senttime;
#endif
};

typedef struct
{
  uint32_t           BaudRate;
  uint32_t           FlowControl;
}ETM_UARTConfig_t;

typedef struct {
  ETM_UARTConfig_t   UART_Config;
  ETM_IO_t           fops;
  App_GetTickCb_Func  GetTickCb;
  uint8_t             CmdResp[ETM_CMD_SIZE];
  struct subtpc subtopics[MAX_SUB_TOPICS];
  struct pubtpc pubtopics[MAX_PUB_TOPICS];
  unsigned int urcseen;
  _atcb atcallback;
  _statecb statecallback;
  unsigned char binaryread;
  unsigned char buffered;
  uint8_t readingsub;
  tetmState currentstate;
}ETMObject_t;

/* Exported functions --------------------------------------------------------*/

/* ==== Init and status ==== */

ETM_Return_t ETM_RegisterTickCb(ETMObject_t *Obj, App_GetTickCb_Func GetTickCb);

ETM_Return_t  ETM_RegisterBusIO(ETMObject_t *Obj, IO_Init_Func IO_Init,
                                                     IO_DeInit_Func IO_DeInit,
                                                     IO_Baudrate_Func IO_Baudrate,
                                                     IO_Send_Func IO_Send,
                                                     IO_ReceiveOne_Func IO_ReceiveOne,
                                                     IO_Flush_Func IO_Flush);

ETM_InitRet_t ETM_Init(ETMObject_t *Obj, _atcb urccallback);
void ETMpoll(ETMObject_t *Obj);

int ETMstartproto(ETMObject_t *Obj, tetmProto proto);

int ETMsubscribe(ETMObject_t *Obj, char *topic, _msgcb callback);

int ETMpubreg(ETMObject_t *Obj, char *topic);
int ETMpublish(ETMObject_t *Obj, int tpcidx, uint8_t qos, uint8_t *data, uint16_t datalen);

/* Application must provide callback function that gives a Timer Tick in ms (e.g. HAL_GetTick())*/
ETM_Return_t  ETM_RegisterTickCb(ETMObject_t *Obj, App_GetTickCb_Func  GetTickCb);

#ifdef __cplusplus
}
#endif
#endif /*__ETM_H */
