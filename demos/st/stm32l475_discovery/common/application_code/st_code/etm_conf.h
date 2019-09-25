/**
  ******************************************************************************
  * @file    etm_conf.h
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
/* --------------------------------------------------------------------------- */   

/* Allow 1024 for large mqtt messages */
#define ETM_CMD_SIZE                           1024                                                      

#define ETM_DEFAULT_BAUDRATE                   115200 

#ifdef __cplusplus
}
#endif
#endif /* __UG96_CONF_H */

