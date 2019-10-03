/**
  ******************************************************************************
  * @file    etm.c
  * @author  Paul Tupper @ Eseye
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#include "etm.h"
#include "etm_conf.h"

static void ETMProcessReceived(ETMObject_t *Obj, uint32_t match);

#ifdef TIMEOUT_RESPONSES
static bool ETMcheckTimeout(ETMObject_t *Obj);
#endif

/* Private variable ---------------------------------------------------------*/
char CmdString[ETM_CMD_SIZE];

/* Exported variable ---------------------------------------------------------*/



//#define ETM_DBG(x...)
#define ETM_DBG_AT(x...)
//#define ETM_DEEP_DBG(x...)

#define UARTDEBUGPRINTF(x...) configPRINTF((x))
#define ETM_DBG(x...)         configPRINTF(x)
//#define ETM_DBG_AT(x...)      configPRINTF(x)
#define ETM_DEEP_DBG(x...)    configPRINTF(x)


const ETM_RetKeywords_t ReturnKeywords[] = {
    { RET_SENDOK,       ":SEND OK\r\n"},
    { RET_SENDFAIL,     ":SEND FAIL\r\n"},
    { RET_IDLE,         "+ETM:IDLE\r\n"},
    { RET_MQTTREC,      "+EMQ:"},
    { RET_EMQRDY,       "+ETM:EMQRDY\r\n"},
    { RET_SUBOPEN,      "+EMQSUBOPEN:"},
    { RET_SUBCLOSE,     "+EMQSUBCLOSE:"},
    { RET_PUBOPEN,      "+EMQPUBOPEN:"},
    { RET_PUBCLOSE,     "+EMQPUBCLOSE:"},
    { RET_EURDY,        "+ETM:EURDY\r\n"},
    { RET_STATEURC,     "+ETMSTATE:"},
    { RET_APPRDY,       "APP RDY"},
    { RET_FWAVAILABLE,  "+ETMHFWGET:"},
    { RET_OK,           "OK\r\n" },
    { RET_ERROR,        "ERROR\r\n" },
    { RET_CRLF,         "\r\n" },
};

/* Private functions ---------------------------------------------------------*/

/**
 * @brief   Return the integer difference between 'init + timeout' and 'now'.
 *          The implementation is robust to uint32_t overflows.
 * @param   In:   init      Reference index.
 * @param   In:   now       Current index.
 * @param   In:   timeout   Target index.
 * @retval  Number of ms from now to target (init + timeout).
 */
static int32_t TimeLeftFromExpiration(uint32_t init, uint32_t now, uint32_t timeout){
  int32_t ret = 0;
  uint32_t wrap_end = 0;

  if (now < init){ /* Timer wrap-around detected */
    wrap_end = UINT32_MAX - init;
  }
  ret = wrap_end - (now - init) + timeout;

  return ret;
}

/* persistScanVals holds bits for scan values which are persistent. i.e. even if a call to AT_RetrieveData
 * does not include these flags they will be scanned for. Any text matching a ScanVal flag will be returned
 * to the caller but any text matching a persistScanVal will be dispatched to ETMProcessReceived().
 */
static uint32_t persistScanVals = 0;

/**
  * @brief  Retrieve Data from the C2C module over the UART interface.
  *         This function receives data from the  C2C module, the
  *         data is fetched from a ring buffer that is asynchronously and continuously
            filled with the received data.
  * @param  Obj: pointer to module handle
  * @param  pData: a buffer inside which the data will be read.
  * @param  Length: Size of receive buffer.
  * @param  ScanVals: values to be retrieved in the coming data in order to exit.
  * @retval int32_t: if ScanVals != 0 : the actual RET_CODE found,
                     if ScanVals = 0 : the actual data size that has been received
                     if error (timeout): return -1 (ETM_RETURN_RETRIEVE_ERROR).
  */
static int32_t AT_RetrieveData(ETMObject_t *Obj, uint8_t* pData, uint16_t Length, uint32_t ScanVals, uint32_t Timeout){
  uint32_t tickstart = Obj->GetTickCb();
  int16_t ReadData = 0;
  uint16_t x;
  uint16_t index[NUM_RESPONSES];
  uint16_t lens[NUM_RESPONSES];
  uint16_t pos;
  uint8_t c;
  int32_t min_requested_time;

  if(Length == 0 && ScanVals == 0){
	  /* Can't have no buffer && scan values */
	  ETM_DBG(("Request to retrieve data with no buffer or scan values\r\n"));
	  return ETM_RETURN_RETRIEVE_ERROR;
  }

  min_requested_time = 100;
  if (Timeout < min_requested_time)       /* UART speed 115200 bits per sec */
  {
     Timeout = min_requested_time;
     //ETM_DBG(("UART_C2C: Timeout forced to respect UART speed %d: %ld\r\n", UG96_DEFAULT_BAUDRATE, min_requested_time));
  }

  for (x = 0; x < NUM_RESPONSES; x++){
    index[x] = 0;
    lens[x] = strlen(ReturnKeywords[x].retstr);
  }

  /* Clear out the response array (implicit null termination) */
  memset(pData, 0, Length);

  /* Read characters from uart buffer until match or timeout */
  while (TimeLeftFromExpiration(tickstart, Obj->GetTickCb(), Timeout) > 0){
    if(Obj->fops.IO_ReceiveOne(&c) == 0){
      /* If we're scanning for fixed strings don't overflow the supplied buffer */
      if(ReadData < Length)
          pData[ReadData++] = c;

      if (ScanVals != 0){
        /* Check whether we hit any ESP return values */
        for(x = 0; x < NUM_RESPONSES; x++){
          if (c == ReturnKeywords[x].retstr[index[x]]){
            pos = ++(index[x]);
            if( pos >= lens[x]){
              if (ScanVals & ReturnKeywords[x].retval){
            	/* We have matched a response - return it here */
                return ReturnKeywords[x].retval;
              }else if(persistScanVals & ReturnKeywords[x].retval){
            	ETMProcessReceived(Obj, ReturnKeywords[x].retval);
            	/* Any collated buffer is no good with URCs embedded so flush */
            	ReadData = 0;
            	memset(pData, 0, Length);
              }
            }
          }else{
        	index[x] = 0;
          }
        }
      }
      if (ReadData >= Length){
        return ReadData;
      }
    }else{
        vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  if ((ScanVals == 0) && (ReadData > 0)){
    ETM_DBG(("AT_Read: Warning: timeout occurred before all data was read (%d/%u)\r\n", ReadData, Length));
    return ReadData;
  }
  //ETM_DEEP_DBG(("AT_Read: RET_CODE not found (%d bytes read)\r\n", ReadData));

  //pData[ReadData] = 0;
  //ETM_DEEP_DBG(("AT_Read: %s\r\n", pData));

  return ETM_RETURN_NO_DATA;
}


static int32_t  AT_ExecuteCommand(ETMObject_t *Obj, uint32_t timeout, uint8_t* cmd, uint32_t resp){
  int32_t ret = ETM_RETURN_SEND_ERROR;

  if (timeout == 0){
    timeout = ETM_TOUT_300;
  }
  ETM_DBG_AT(("AT Request: %s\r\n", cmd));
  if(Obj->fops.IO_Send(cmd, strlen((char*)cmd)) >= 0){
    ret = (AT_RetrieveData(Obj, Obj->CmdResp, ETM_CMD_SIZE, resp, timeout));
    if (ret < RET_NONE)    {
      //ETM_DBG(("ETM AT_ExecuteCommand() rcv TIMEOUT ret=%ld: %s\r\n", ret, cmd));
    }else{
      ETM_DBG_AT(("AT Response: %s\r\n", Obj->CmdResp));
    }
  }else{
    ETM_DBG(("ETM AT_ExecuteCommand() send ERROR: %s\r\n", cmd));
  }
  return ret;
}

#ifdef removed
static int32_t AT_Synchro(ETMObject_t *Obj){
  int32_t ret = ETM_RETURN_SEND_ERROR;
  int8_t atSync = 0;
  uint32_t tickstart;

  /* Init tickstart for timeout management */
  tickstart = Obj->GetTickCb();

  /* Start AT SYNC: Send AT every 500ms,
  if receive OK, SYNC success,
  if no OK return after sending AT 10 times, SYNC fail */
  do {
    if (TimeLeftFromExpiration(tickstart, Obj->GetTickCb(), ETM_TOUT_ATSYNC) < 0) {
      ret = AT_ExecuteCommand(Obj, ETM_TOUT_SHORT, (uint8_t *)"AT\r\n", RET_OK | RET_ERROR);
      atSync++;
      tickstart = Obj->GetTickCb();
    }
  }
  while ((atSync < 10) && (ret != RET_OK));

  return ret;
}
#endif

/* --------------------------------------------------------------------------*/
/* --- Public functions -----------------------------------------------------*/
/* --------------------------------------------------------------------------*/

ETM_Return_t  ETM_RegisterBusIO(ETMObject_t *Obj, IO_Init_Func        IO_Init,
                                                     IO_DeInit_Func      IO_DeInit,
                                                     IO_Baudrate_Func    IO_Baudrate,
                                                     IO_Send_Func        IO_Send,
                                                     IO_ReceiveOne_Func  IO_ReceiveOne,
                                                     IO_Flush_Func       IO_Flush)
{
  if(!Obj || !IO_Init || !IO_DeInit || !IO_Baudrate || !IO_Send || !IO_ReceiveOne || !IO_Flush){
    return ETM_RETURN_ERROR;
  }

  Obj->fops.IO_Init = IO_Init;
  Obj->fops.IO_DeInit = IO_DeInit;
  Obj->fops.IO_Baudrate = IO_Baudrate;
  Obj->fops.IO_Send = IO_Send;
  Obj->fops.IO_ReceiveOne = IO_ReceiveOne;
  Obj->fops.IO_FlushBuffer = IO_Flush;

  return ETM_RETURN_OK;
}

ETM_Return_t ETM_RegisterTickCb(ETMObject_t *Obj, App_GetTickCb_Func GetTickCb){
  if(!Obj || !GetTickCb){
    return ETM_RETURN_ERROR;
  }

  Obj->GetTickCb = GetTickCb;

  return ETM_RETURN_OK;
}

ETM_InitRet_t ETM_Init(ETMObject_t *Obj, _atcb urccallback){
  ETM_InitRet_t fret = ETM_INIT_OTHER_ERR;
//  int32_t ret = RET_ERROR;
  uint32_t tickstart;

  ETM_DBG(("ETM init\r\n"));

  Obj->fops.IO_FlushBuffer();  /* Flush Uart intermediate buffer */

  if (Obj->fops.IO_Init() == 0) /* configure and initialize UART */
  {
//    ret = AT_Synchro(Obj);

//    if(ret != RET_OK){
//      ETM_DBG(("Fail to AT SYNC, after several attempts\r\n"));
//      fret = ETM_INIT_RET_AT_ERR; /* if does not respond to AT command set specific return status */
//    }else{

      int i;
      for(i = 0; i < MAX_SUB_TOPICS; i++){
        Obj->subtopics[i].messagecb = NULL;
        Obj->subtopics[i].substate = SUB_TOPIC_NOT_IN_USE;
      }
      for(i = 0; i < MAX_PUB_TOPICS; i++){
        Obj->pubtopics[i].pubstate = PUB_TOPIC_NOT_IN_USE;
      }

      Obj->atcallback = urccallback;
      Obj->binaryread = 0;
      Obj->buffered = 0;
      Obj->readingsub = 0xff;
      Obj->urcseen = 0;
      Obj->currentstate = ETM_UNKNOWN;
      Obj->statecallback = NULL;
      Obj->fwupdcb = NULL;
    
//    }

      ETM_DBG(("ETM waiting...\r\n"));
      /* Wait for ETM:IDLE */
      tickstart = Obj->GetTickCb();
      while((Obj->GetTickCb() - tickstart) < pdMS_TO_TICKS(15000) && !(Obj->urcseen & ETM_READY_URC)){
          ETMpoll(Obj);
      }
  }else{
      fret = ETM_INIT_RET_IO_ERR;
  }

  if(Obj->urcseen & ETM_READY_URC){
	  ETM_DBG(("ETM is ready\r\n"));
  }else{
	  ETM_DBG(("ETM NOT ready\r\n"));
  }

  return fret;
}

/* Subscribe topic API */

/* Subscribe to a topic using the first available topic index */
int ETMsubscribe(ETMObject_t *Obj, char *topic, _msgcb callback){
  int topiccount = 0;
  uint32_t ret;
#ifdef TIMEOUT_RESPONSES
  ETMcheckTimeout(Obj);
#endif
  while(Obj->subtopics[topiccount].substate != SUB_TOPIC_NOT_IN_USE && Obj->subtopics[topiccount].substate != SUB_TOPIC_ERROR && topiccount < MAX_SUB_TOPICS){
    topiccount++;
  }
  if(topiccount == MAX_SUB_TOPICS)
    return -1;
  UARTDEBUGPRINTF("Subscribe to %s\r\n", topic);
  
  sprintf(CmdString, "AT+EMQSUBOPEN=%d,\"%s\"\r\n", topiccount, topic);
  ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)CmdString, RET_OK | RET_ERROR);
  if(ret == RET_OK){
    Obj->subtopics[topiccount].substate = SUB_TOPIC_SUBSCRIBING;
    Obj->subtopics[topiccount].messagecb = callback;
  }
  return topiccount;
}

/* Have we successfully subscribed */
tsubTopicState ETMsubstate(ETMObject_t *Obj, int idx){
#ifdef TIMEOUT_RESPONSES
  ETMcheckTimeout(Obj);
#endif
  return Obj->subtopics[idx].substate;
}

/* Unsubscribe from a topic index */
int ETMunsubscribe(ETMObject_t *Obj, int idx){
  uint32_t ret;
#ifdef TIMEOUT_RESPONSES
  ETMcheckTimeout(Obj);
#endif
  if(idx >= MAX_SUB_TOPICS)
    return -1;
  if(Obj->subtopics[idx].substate == SUB_TOPIC_SUBSCRIBED){
    sprintf(CmdString, "AT+EMQSUBCLOSE=%d\r\n", idx);
    ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)CmdString, RET_OK | RET_ERROR);
    if(ret == RET_OK){
      Obj->subtopics[idx].substate = SUB_TOPIC_UNSUBSCRIBING;
    }  
    return 0;
  }
  return -1;
}

/* Publish topic API */

#define TOPIC_NOT_REGISTERED 0
#define TOPIC_REGISTERING    1
#define TOPIC_REGISTERED     2

/* Register a publish topic */
int ETMpubreg(ETMObject_t *Obj, char *topic){
  uint32_t ret;
  int topiccount = 0;
#ifdef TIMEOUT_RESPONSES
  ETMcheckTimeout(Obj);
#endif
  while(Obj->pubtopics[topiccount].pubstate != PUB_TOPIC_NOT_IN_USE && Obj->pubtopics[topiccount].pubstate != PUB_TOPIC_ERROR && topiccount < MAX_PUB_TOPICS){
    topiccount++;
  }
  if(topiccount == MAX_PUB_TOPICS)
    return -1;
  sprintf(CmdString, "AT+EMQPUBOPEN=%d,\"%s\"\r\n", topiccount, topic);
  ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)CmdString, RET_OK | RET_ERROR);
  if(ret == RET_OK){
    UARTDEBUGPRINTF("Pubreg %s\r\n", topic);

    Obj->pubtopics[topiccount].pubstate = PUB_TOPIC_REGISTERING;
#ifdef TIMEOUT_RESPONSES
    Obj->pubtopics[topiccount].senttime = Obj->GetTickCb();
#endif
  }
  return topiccount;
}

/* Check if publish topic is registered */
tpubTopicState ETMpubstate(ETMObject_t *Obj, int idx){
#ifdef TIMEOUT_RESPONSES
  ETMcheckTimeout(Obj);
#endif
  return Obj->pubtopics[idx].pubstate;
}

/* Unregister a publish topic */
int ETMpubunreg(ETMObject_t *Obj, int idx){
  uint32_t ret;
#ifdef TIMEOUT_RESPONSES
  ETMcheckTimeout(Obj);
#endif
  if(idx >= MAX_PUB_TOPICS)
    return -1;
  if(Obj->pubtopics[idx].pubstate == PUB_TOPIC_REGISTERED){
    sprintf(CmdString, "AT+EMQPUBCLOSE=%d\r\n", idx);
    ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)CmdString, RET_OK | RET_ERROR);
    if(ret == RET_OK){
      Obj->pubtopics[idx].pubstate = PUB_TOPIC_UNREGISTERING; 
#ifdef TIMEOUT_RESPONSES
      Obj->pubtopics[idx].senttime = Obj->GetTickCb();
#endif
    }
    return 0;
  }
  return -1;
}

/* Given an octet convert it to a two-character string ascii-hex representation */
static void octettohex(uint8_t octet, char *dest){
    uint8_t nibble = (octet >> 4) & 0x0f;
    if(nibble < 10)
        dest[0] = '0' + nibble;
    else
        dest[0] = 'A' + (nibble - 10);
    nibble = octet & 0x0f;
    if(nibble < 10)
        dest[1] = '0' + nibble;
    else
        dest[1] = 'A' + (nibble - 10);
    dest[2] = 0;
}

/* Take two characters and convert to an octet assuming they are ascii-hex */
static int hextooctet(char *hex){
	int octet;
	if(hex[0] >= '0' && hex[0] <= '9')
		octet = (hex[0] - '0') << 4;
	else if(hex[0] >= 'a' && hex[0] <= 'f')
		octet = (hex[0] - 'a' + 10) << 4;
	else if(hex[0] >= 'A' && hex[0] <= 'F')
		octet = (hex[0] - 'A' + 10) << 4;
	else
		return -1;
	if(hex[1] >= '0' && hex[1] <= '9')
		octet |= (hex[1] - '0');
	else if(hex[1] >= 'a' && hex[1] <= 'f')
		octet |= (hex[1] - 'a' + 10);
	else if(hex[1] >= 'A' && hex[1] <= 'F')
		octet |= (hex[1] - 'A' + 10);
	else
		octet = -1;
	return octet;
}

/* Publish a message to a topic by index */
int ETMpublish(ETMObject_t *Obj, int tpcidx, uint8_t qos, uint8_t *data, uint16_t datalen){
  char hexbyte[3];
  uint32_t ret;
#ifdef TIMEOUT_RESPONSES
  ETMcheckTimeout(Obj);
#endif  
  /* TODO - check we're not already sending something */
  
  if(tpcidx < MAX_PUB_TOPICS && Obj->pubtopics[tpcidx].pubstate == PUB_TOPIC_REGISTERED){   
    sprintf(CmdString, "AT+EMQPUBLISH=%d,%d,\"", tpcidx, qos);

    UARTDEBUGPRINTF("Publishing %s to idx %d\r\n", (char *)data, tpcidx);

    if(Obj->fops.IO_Send((uint8_t *)CmdString, strlen((char*)CmdString)) >= 0){
        /* Convert data to ascii-hex */
        for(uint16_t countlen=0; countlen < datalen; countlen++){
            octettohex(data[countlen], hexbyte);
            if(Obj->fops.IO_Send((uint8_t *)hexbyte, 2) < 0){
                /* This is an error */
                
            }
        }
        ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)"\"\r\n", RET_OK | RET_ERROR);
        if(ret == RET_OK){
            return 0;
        }
    }
  }else{
	  UARTDEBUGPRINTF("Topic %d not registered (%d)\r\n", tpcidx, Obj->pubtopics[tpcidx].pubstate);
  }
  return -1;
}

/* Polling loop - the work is done here */
void ETMpoll(ETMObject_t *Obj){
  persistScanVals = RET_SENDOK | RET_SENDFAIL | RET_IDLE | RET_CRLF | RET_MQTTREC | RET_EMQRDY | RET_SUBOPEN | RET_SUBCLOSE | RET_PUBOPEN | RET_PUBCLOSE | RET_EURDY | RET_STATEURC | RET_APPRDY | RET_FWAVAILABLE;
#ifdef TIMEOUT_RESPONSES
  ETMcheckTimeout(Obj);
#endif 
  
  AT_RetrieveData(Obj, Obj->CmdResp, ETM_CMD_SIZE, RET_ANY, ETM_TOUT_300);
  
}

static void ETMProcessReceived(ETMObject_t *Obj, uint32_t match){
  int32_t ret;
  char *errptr;
  uint8_t idx;
  int8_t err;

  //if(ret != RET_NONE && ret != ETM_RETURN_RETRIEVE_ERROR)
	//  UARTDEBUGPRINTF("Received URC >%s<\r\n", Obj->CmdResp);

  switch(match){
      case RET_IDLE:
          /* ETM is ready */
          Obj->urcseen |= ETM_READY_URC;
          UARTDEBUGPRINTF("ETM READY\r\n");
          break;
      case RET_EMQRDY:
          /* MQTT mode is ready */
          Obj->urcseen |= ETM_MQTTREADY_URC;
          UARTDEBUGPRINTF("MQTT READY\r\n");
          break;
      case RET_EURDY:
          /* UDP mode is ready */
          Obj->urcseen |= ETM_UDPREADY_URC;
          break;
          
      case RET_STATEURC:
    	  ret = AT_RetrieveData(Obj, Obj->CmdResp, ETM_CMD_SIZE, RET_CRLF, ETM_TOUT_300);
    	  if(ret == RET_CRLF){
              Obj->currentstate = (tetmState)strtol((char *)Obj->CmdResp, NULL, 10);
              if(Obj->statecallback != NULL)
                  Obj->statecallback();
    	  }
          break;
      
      case RET_SUBOPEN:
    	  /* We've got the start of a subopen urc - now get the status */
    	  ret = AT_RetrieveData(Obj, Obj->CmdResp, ETM_CMD_SIZE, RET_CRLF, ETM_TOUT_300);
          if(ret == RET_CRLF){
    	      idx = strtol((char *)Obj->CmdResp, &errptr, 10);
              err = strtol(errptr + 1, NULL, 10);
              UARTDEBUGPRINTF("subscribe %d err %d\r\n", idx, err);
              /* If we get an already subscribed error assume it was us from before a reboot */
              if(err == 0 || err == -2)
                  Obj->subtopics[idx].substate = SUB_TOPIC_SUBSCRIBED;
              else
                  Obj->subtopics[idx].substate = SUB_TOPIC_ERROR;
          }else{
        	  UARTDEBUGPRINTF("Error decoding subopen urc\r\n");
          }
          break;
      case RET_SUBCLOSE:
    	  ret = AT_RetrieveData(Obj, Obj->CmdResp, ETM_CMD_SIZE, RET_CRLF, ETM_TOUT_300);
    	  if(ret == RET_CRLF){
              idx = strtol((char *)Obj->CmdResp, &errptr, 10);
              err = strtol(errptr + 1, NULL, 10);
              UARTDEBUGPRINTF("unsubscribe %d err %d\r\n", idx, err);
              Obj->subtopics[idx].substate = SUB_TOPIC_NOT_IN_USE;
    	  }else{
    		  UARTDEBUGPRINTF("Error decoding subclose urc\r\n");
    	  }
          break;
      case RET_PUBOPEN:
    	  ret = AT_RetrieveData(Obj, Obj->CmdResp, ETM_CMD_SIZE, RET_CRLF, ETM_TOUT_300);
    	  if(ret == RET_CRLF){
              idx = strtol((char *)Obj->CmdResp, &errptr, 10);
              err = strtol(errptr + 1, NULL, 10);
              UARTDEBUGPRINTF("pubreg %d err %d\r\n", idx, err);
              /* If we get an already registered error assume it was us from before a reboot */
              if(err == 0 || err == -2)
                  Obj->pubtopics[idx].pubstate = PUB_TOPIC_REGISTERED;
              else
                  Obj->pubtopics[idx].pubstate = PUB_TOPIC_ERROR;
    	  }else{
    		  UARTDEBUGPRINTF("Error decoding pubopen urc (ret %d)\r\n", ret);
    	  }
          break;
      case RET_PUBCLOSE:
    	  ret = AT_RetrieveData(Obj, Obj->CmdResp, ETM_CMD_SIZE, RET_CRLF, ETM_TOUT_300);
    	  if(ret == RET_CRLF){
              idx = strtol((char *)Obj->CmdResp, &errptr, 10);
              err = strtol(errptr + 1, NULL, 10);
              UARTDEBUGPRINTF("pubunreg %d err %d\r\n", idx, err);
              Obj->pubtopics[idx].pubstate = PUB_TOPIC_NOT_IN_USE;
    	  }else{
    		  UARTDEBUGPRINTF("Error decoding pubclose urc\r\n");
    	  }
          break;
      case RET_MQTTREC:
      {
          /* Handle received mqtt here */
          /* <idx>,<len>\r\n<message>\r\n
           * If message is quoted it's ascii-hex
           * len is number of binary octets so x2 for number of ascii characters within quotes */
    	  char *nextptr;
    	  int len;

    	  ret = AT_RetrieveData(Obj, Obj->CmdResp, ETM_CMD_SIZE, RET_CRLF, ETM_TOUT_300);
    	  if(ret == RET_CRLF){
    	      idx = strtol((char *)Obj->CmdResp, &nextptr, 10);
    	      len = strtol(nextptr + 1, &nextptr, 10);

    	      //UARTDEBUGPRINTF("mqtt received >%s<\r\n", Obj->CmdResp);

    	      /* TODO - check validity of len */

    	      ret = AT_RetrieveData(Obj, Obj->CmdResp, len, RET_NONE, ETM_TOUT_300);
    	      if(ret == len){
    	          if(Obj->subtopics[idx].substate == SUB_TOPIC_SUBSCRIBED){
    		          uint8_t *msg;
    		          uint32_t msglen = 0;
    		          nextptr = (char *)Obj->CmdResp;
    	              //while(*nextptr != 0 && (*nextptr == '\r' || *nextptr == '\n'))
    		          //    nextptr++;
    	              if(*nextptr != 0){
    	    	          msg = (uint8_t *)nextptr;
    		              if(*nextptr == '"'){
    		    	          uint8_t *writemsg = ++msg;
    			              /* This is in ascii-hex format but len indicates the number of binary octets */
    		    	          /* Overwrite the received (char)string with (uint8_t)binary data. This works
    		    	           * as there are two characters for each binary octet. */
    		    	          while(msglen < len && msg[msglen * 2] != '"'){
    		    	              *writemsg = hextooctet((char *)&msg[msglen * 2]);
    		    	              if(*writemsg == -1){
    		    		              /* Error - fail here */
    		    	    	          UARTDEBUGPRINTF("Error decoding ascii-hex message for %d (%d characters expected - %d found)\r\n", idx, len * 2, msglen * 2);
    		    		              msg = NULL;
    		    		              break;
    		    	              }
    		    	              writemsg++;
    		    	              msglen++;
    		    	          }
    		              }else{
    			              /* This is ascii data */
                              msg[len] = 0;
                              msglen = len;
    		              }
    	                  if(msg != NULL && msglen > 0 && Obj->subtopics[idx].messagecb != NULL)
    	    	              Obj->subtopics[idx].messagecb(msg, msglen);
    	              }
    	          }else{
    		          UARTDEBUGPRINTF("Received publish on non-subscribed topic %d\r\n", idx);
    	          }
    	      }else{
    	    	  UARTDEBUGPRINTF("Failed to read %d published octets\r\n", len);
    	      }
    	  }

      }
          break;
          
      case RET_SENDOK:
          UARTDEBUGPRINTF("Send OK\r\n");
          break;
          
      case RET_SENDFAIL:
          UARTDEBUGPRINTF("Send Fail\r\n");
          break;
          
      case RET_APPRDY:
          AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)"ATE0\r\n", RET_OK | RET_ERROR);
          UARTDEBUGPRINTF("BG96 found\r\n");
          break;
      case RET_FWAVAILABLE:
    	  ret = AT_RetrieveData(Obj, Obj->CmdResp, ETM_CMD_SIZE, RET_CRLF, ETM_TOUT_500);
    	  if(ret == RET_CRLF){
    		  bool firmware_available = false;
    		  if(strncmp((char *)Obj->CmdResp, "available", 9) == 0){
    			  UARTDEBUGPRINTF("Host firmware available\r\n");
    			  firmware_available = true;
    		  }else{
    			  UARTDEBUGPRINTF("Host firmware not available\r\n");
    		  }
    		  if(Obj->fwupdcb != NULL)
    			  Obj->fwupdcb(firmware_available);
    		  Obj->fwupdcb = NULL;
    	  }
    	  break;
      case RET_CRLF:
    	  /* Ignore newlines */
    	  break;
      default:
    	  if(match > RET_NONE)
    	      UARTDEBUGPRINTF("Got URC %d\r\n", match);
    	  break;
  }
}

/* Send an AT command */
void ETMsendAT(ETMObject_t *Obj, char *atcmd){
#ifdef TIMEOUT_RESPONSES
    checkTimeout(Obj);
#endif
    Obj->fops.IO_Send((uint8_t *)atcmd, strlen(atcmd));
}

void ETMupdateState(ETMObject_t *Obj, tetmRequestState streq){
    uint32_t ret = RET_NONE;
    if(streq == ETM_STATE_ONCE){
        Obj->currentstate = ETM_UNKNOWN;
        ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)"AT+ETMSTATE?\r\n", RET_OK | RET_ERROR);
    }else if(streq == ETM_STATE_ON){
        ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)"AT+ETMSTATE=1\r\n", RET_OK | RET_ERROR);
    }else{
        ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)"AT+ETMSTATE=0\r\n", RET_OK | RET_ERROR);
    }
    if(ret == RET_ERROR){
        /* This is an error */
        
    }
}

void ETMstatecb(ETMObject_t *Obj, _statecb stateupdatecb){
    Obj->statecallback = stateupdatecb;
}

int ETMstartproto(ETMObject_t *Obj, tetmProto proto){
    uint32_t ret = RET_NONE;
    if(proto == ETM_MQTT){
        ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)"AT+ETMSTATE=startmqtt\r\n", RET_OK | RET_ERROR);
    }else if (proto == ETM_UDP){
        ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)"AT+ETMSTATE=startudp\r\n", RET_OK | RET_ERROR);
    }else{
        return -1;
    }
    if(ret == RET_ERROR){
        /* Error */
        return -1;
    }
    return 0;
}

#ifdef TIMEOUT_RESPONSES
static bool ETMcheckTimeout(ETMObject_t *Obj){
    /* Check pending pub/sub requests etc. */
    int topiccount = 0;
    bool waiting = false;
    uint32_t now = Obj->GetTickCb(), diff;
    while(topiccount < MAX_PUB_TOPICS){
        diff = now - Obj->pubtopics[topiccount].senttime;
        if(Obj->pubtopics[topiccount].pubstate == PUB_TOPIC_REGISTERING || Obj->pubtopics[topiccount].pubstate == PUB_TOPIC_UNREGISTERING){
            if(diff >= pdMS_TO_TICKS(PUB_TIMEOUT)){
                Obj->pubtopics[topiccount].pubstate = PUB_TOPIC_ERROR;
                UARTDEBUGPRINTF("Pub idx %d timed out\n", topiccount);
            }else{
                waiting = true;
            }
        }
        topiccount++;
    }
    return waiting;
}
#endif

int ETMGetHostFW(ETMObject_t *Obj, char *url, _fwupdcb cb){
	int rc = -1;
	uint32_t ret = RET_OK;
	if(url != NULL){
	    sprintf(CmdString, "AT+ETMCFG=host,updateurl,%s\r\n", url);
	    ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)CmdString, RET_OK | RET_ERROR);
	}
	if(ret == RET_OK){
	    sprintf(CmdString, "AT+ETMHFWGET\r\n");
	    ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)CmdString, RET_OK | RET_ERROR);
	    Obj->fwupdcb = cb;
	    rc = 0;
	}
	return rc;
}

int ETMGetHostFWDetails(ETMObject_t *Obj, uint32_t *len, uint16_t *cs){
	uint32_t ret = RET_NONE;
	int rc = -1;
	persistScanVals &= ~RET_CRLF;
    sprintf(CmdString, "AT+ETMHFWREAD?\r\n");
    ret = AT_ExecuteCommand(Obj, ETM_TOUT_300, (uint8_t *)CmdString, RET_OK | RET_ERROR);
    persistScanVals |= RET_CRLF;
    if(ret == RET_OK){
    	char *parsestr = (char *)Obj->CmdResp;

    	configPRINTF(("Response is %s\r\n", parsestr));

    	while(*parsestr != ':' && *parsestr != 0)
    		parsestr++;
    	if(*parsestr == ':'){
    		parsestr++;
    		*len = strtol(parsestr, &parsestr, 10);
    		if(*parsestr == ','){
    			parsestr++;
    			*cs = strtol(parsestr, &parsestr, 16);
    			rc = 0;
    		}
    	}
    }
    return rc;
}

int ETMReadHostFW(ETMObject_t *Obj, uint32_t offset, uint16_t len, uint8_t *respbuf){
	int rc = -1, octets = 0;
	uint32_t ret = RET_OK;
	persistScanVals &= ~RET_CRLF;
	sprintf(CmdString, "AT+ETMHFWREAD=%d,%d\r\n", offset, len);
	ret = AT_ExecuteCommand(Obj, ETM_TOUT_500, (uint8_t *)CmdString, RET_OK | RET_ERROR);
	persistScanVals |= RET_CRLF;
	if(ret == RET_OK){
		char *parsestr = (char *)Obj->CmdResp;
		while(*parsestr != ':' && *parsestr != 0)
			parsestr++;
		if(*parsestr == ':'){
			parsestr++;
			/* Now decode ascii hex into binary */
            while(1){
            	if(isxdigit(parsestr[0]) && isxdigit(parsestr[1])){
            		*respbuf++ = hextooctet(parsestr);
            		parsestr += 2;
            		octets++;
            	}else{
            		break;
            	}

            }
		}
		//UARTDEBUGPRINTF("Got %d octets\r\n", octets);
		if(octets == len)
	        rc = 0;
	}
	return rc;
}
  
