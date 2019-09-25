/**
  ******************************************************************************
  * @file    etm_conf_template.h
  * @author  Paul Tupper @ Eseye
  ******************************************************************************
  */

#ifndef __ETM_CONF_H
#define __ETM_CONF_H

#ifdef __cplusplus
 extern "C" {
#endif  

/* --------------------------------------------------------------------------- */   
/* ETM static parameter that can be configured by the user before compilation */ 
/* --------------------------------------------------------------------------- */                     2

/* 256 is normally sufficient, but if some params like URL are very long string it can be increased*/
#define ETM_CMD_SIZE                           256                                                      

#define ETM_DEFAULT_BAUDRATE                   115200 

/* Rx and Tx buffer size, depend as the applic handles the buffer */
#define ETM_TX_DATABUF_SIZE                    1460 
#define ETM_RX_DATABUF_SIZE                    1500                        1

#ifdef __cplusplus
}
#endif
#endif /* __ETM_CONF_H */

