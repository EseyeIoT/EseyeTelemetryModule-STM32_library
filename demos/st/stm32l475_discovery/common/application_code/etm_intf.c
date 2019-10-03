
#ifdef USE_ESEYE
#include "FreeRTOS.h"
#include "task.h"
#include "message_buffer.h"

#include "etm/etm.h"
#include "etm_io.h"

#include "etm_intf.h"

#include <stdbool.h>
#include <stdlib.h>

#define RING_BUFFER_SIZE 2048

typedef struct
{
  uint8_t  data[RING_BUFFER_SIZE];
  uint16_t tail;
  uint16_t head;
}RingBuffer_t;

static void UART_C2C_MspInit(UART_HandleTypeDef *hUART_c2c);
UART_HandleTypeDef huart4;
RingBuffer_t UART_RxData;

/***********************************************************************/

uint32_t InitSensors(void)
{
	if(BSP_PSENSOR_Init() != PSENSOR_OK) return 0;
	if(BSP_HSENSOR_Init() != HSENSOR_OK) return 0;
	if(BSP_TSENSOR_Init() != TSENSOR_OK) return 0;
	return 1;
}

/* Initialise ETM control hardware */
void ETM_HwStatusInit(void){
	GPIO_InitTypeDef  GPIO_InitStructPwr, GPIO_InitStructStatus;

//	C2C_RST_GPIO_CLK_ENABLE();
	C2C_PWRKEY_GPIO_CLK_ENABLE();
	C2C_STATUS_GPIO_CLK_ENABLE();

	/* STATUS */
	GPIO_InitStructStatus.Pin       = C2C_STATUS_PIN;
	GPIO_InitStructStatus.Mode      = GPIO_MODE_INPUT;
	GPIO_InitStructStatus.Pull      = GPIO_PULLDOWN;
	GPIO_InitStructStatus.Speed     = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(C2C_STATUS_GPIO_PORT, &GPIO_InitStructStatus);

	/* PWRKEY */
	GPIO_InitStructPwr.Pin       = C2C_PWRKEY_PIN;
	GPIO_InitStructPwr.Mode      = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStructPwr.Pull      = GPIO_NOPULL;
	GPIO_InitStructPwr.Speed     = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(C2C_PWRKEY_GPIO_PORT,&GPIO_InitStructPwr);
	HAL_GPIO_WritePin(C2C_PWRKEY_GPIO_PORT, C2C_PWRKEY_PIN,GPIO_PIN_RESET);
	//vTaskDelay(pdMS_TO_TICKS(1000));

	if(HAL_GPIO_ReadPin(C2C_STATUS_GPIO_PORT, C2C_STATUS_PIN) == 1){
		ETM_HwPowerDown();
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}

/* Read the status pin (high indicates ETM is powered on) */
int ETM_HwStatus(void){
	return HAL_GPIO_ReadPin(C2C_STATUS_GPIO_PORT, C2C_STATUS_PIN);
}

/* Power the ETM down (this assumes it is powered up currently) */
void ETM_HwPowerDown(void){
	int waitcount = 12; /* Wait max 3 seconds for modem to power down */
	HAL_GPIO_WritePin(C2C_PWRKEY_GPIO_PORT, C2C_PWRKEY_PIN,GPIO_PIN_SET);
	/* Hold pwrkey for 2 seconds to power down */
	vTaskDelay(pdMS_TO_TICKS(2000));
	HAL_GPIO_WritePin(C2C_PWRKEY_GPIO_PORT, C2C_PWRKEY_PIN,GPIO_PIN_RESET);
	/* Wait for status pin to go low */
	while(HAL_GPIO_ReadPin(C2C_STATUS_GPIO_PORT, C2C_STATUS_PIN) == 1 && waitcount-- > 0)
		vTaskDelay(pdMS_TO_TICKS(250));
}

/* Power the ETM up (this assumes it is currently powered down) */
static void ETM_HwPowerUp(void){
	HAL_GPIO_WritePin(C2C_PWRKEY_GPIO_PORT, C2C_PWRKEY_PIN,GPIO_PIN_SET);
	/* Hold pwrkey for 100 mS */
	vTaskDelay(pdMS_TO_TICKS(100));
	HAL_GPIO_WritePin(C2C_PWRKEY_GPIO_PORT, C2C_PWRKEY_PIN,GPIO_PIN_RESET);
}

static void UART_C2C_MspDeInit(UART_HandleTypeDef *hUART_c2c)
{
  //static DMA_HandleTypeDef hdma_tx;

  /*##-1- Reset peripherals ##################################################*/
  UART_C2C_FORCE_RESET();
  UART_C2C_RELEASE_RESET();

  /*##-2- Disable peripherals and GPIO Clocks #################################*/
  /* Configure UART Tx as alternate function  */
  HAL_GPIO_DeInit(UART_C2C_TX_GPIO_PORT, UART_C2C_TX_PIN);
  /* Configure UART Rx as alternate function  */
  HAL_GPIO_DeInit(UART_C2C_RX_GPIO_PORT, UART_C2C_RX_PIN);

  /*##-3- Disable the DMA Channels ###########################################*/
  /* De-Initialize the DMA Channel associated to transmission process */
  //HAL_DMA_DeInit(&hdma_tx);

  /*##-4- Disable the NVIC for DMA ###########################################*/
  HAL_NVIC_DisableIRQ(UART_C2C_DMA_TX_IRQn);
}

int8_t UART_C2C_Init(void)
{
  /* Set the C2C USART configuration parameters on MCU side */
  /* Attention: make sure the module uart is configured with the same values */
	huart4.Instance        = UART4;
	huart4.Init.BaudRate   = ETM_DEFAULT_BAUDRATE;
	huart4.Init.WordLength = UART_WORDLENGTH_8B;
	huart4.Init.StopBits   = UART_STOPBITS_1;
	huart4.Init.Parity     = UART_PARITY_NONE;
	huart4.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
	huart4.Init.Mode       = UART_MODE_TX_RX;
	huart4.Init.OverSampling = UART_OVERSAMPLING_16;
	huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  UART_C2C_MspInit(&huart4);
  /* Configure the USART IP */
  if(HAL_UART_Init(&huart4) != HAL_OK)
  {
    return -1;
  }

  /* Once the C2C UART is initialized, start an asynchronous recursive
   listening. the HAL_UART_Receive_IT() call below will wait until one char is
   received to trigger the HAL_UART_RxCpltCallback(). The latter will recursively
   call the former to read another char.  */
  UART_RxData.head = 0;
  UART_RxData.tail = 0;
  HAL_UART_Receive_IT(&huart4, (uint8_t *)&UART_RxData.data[UART_RxData.tail], 1);

  return 0;
}

static void UART_C2C_MspInit(UART_HandleTypeDef *hUART_c2c)
{
  //static DMA_HandleTypeDef hdma_tx;
  GPIO_InitTypeDef  GPIO_Init;

  /* Enable the GPIO clock */
  /* C2C_RST_GPIO_CLK_ENABLE(); */


  /* Set the GPIO pin configuration parametres */
//  GPIO_Init.Pin       = C2C_RST_PIN;
//  GPIO_Init.Mode      = GPIO_MODE_OUTPUT_PP;
//  GPIO_Init.Pull      = GPIO_PULLUP;
//  GPIO_Init.Speed     = GPIO_SPEED_HIGH;
//
//  /* Configure the RST IO */
//  HAL_GPIO_Init(C2C_RST_GPIO_PORT, &GPIO_Init);

  /* Enable DMA clock */
  //DMAx_CLK_ENABLE();

  /*##-1- Enable peripherals and GPIO Clocks #################################*/
  /* Enable GPIO TX/RX clock */
  //HAL_PWREx_EnableVddIO2(); /* needed for GPIO PGxx on L496AG*/
  UART_C2C_TX_GPIO_CLK_ENABLE();
  UART_C2C_RX_GPIO_CLK_ENABLE();

  /* Enable UART_C2C clock */
  UART_C2C_CLK_ENABLE();

  /* Enable DMA clock */
  //DMAx_CLK_ENABLE();

  /*##-2- Configure peripheral GPIO ##########################################*/
  /* UART TX GPIO pin configuration  */
  GPIO_Init.Pin       = UART_C2C_TX_PIN|UART_C2C_RX_PIN;
  GPIO_Init.Mode      = GPIO_MODE_AF_PP;
  GPIO_Init.Pull      = GPIO_NOPULL;
  GPIO_Init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_Init.Alternate = UART_C2C_TX_AF;

  HAL_GPIO_Init(UART_C2C_TX_GPIO_PORT, &GPIO_Init);

  /* UART RX GPIO pin configuration  */
 // GPIO_Init.Pin = UART_C2C_RX_PIN;
  //GPIO_Init.Alternate = UART_C2C_RX_AF;

  //HAL_GPIO_Init(UART_C2C_RX_GPIO_PORT, &GPIO_Init);


  /*##-3- Configure the NVIC for UART ########################################*/
  HAL_NVIC_SetPriority(UART_C2C_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(UART_C2C_IRQn);

}

int8_t UART_C2C_DeInit(void)
{
  /* Reset USART configuration to default */
  HAL_UART_DeInit(&huart4);
  UART_C2C_MspDeInit(&huart4);

  return 0;
}

/**
  * @brief  C2C IO change baudrate
  *         To be used just in case the UG96_DEFAULT_BAUDRATE need to be changed
  *         This function has to be called after having changed the C2C module baudrate
  *         In order to do that the SMT32 Init shall be done at UG96_DEFAULT_BAUDRATE
  *         After C2C module baudrate is changed this function sets the STM32 baudrate accordingly
  * @param  None.
  * @retval 0 on success, -1 otherwise.
  */
int8_t UART_C2C_SetBaudrate(uint32_t BaudRate)
{
  HAL_UART_DeInit(&huart4);
  huart4.Init.BaudRate   = BaudRate;
  if(HAL_UART_Init(&huart4) != HAL_OK)
  {
    return -1;
  }
  /* Once the C2C UART is initialized, start an asynchronous recursive
   listening. the HAL_UART_Receive_IT() call below will wait until one char is
   received to trigger the HAL_UART_RxCpltCallback(). The latter will recursively
   call the former to read another char.  */
  UART_RxData.head = 0;
  UART_RxData.tail = 0;
  HAL_UART_Receive_IT(&huart4, (uint8_t *)&UART_RxData.data[UART_RxData.tail], 1);

  return 0;
}


/**
  * @brief  Flush Ring Buffer
  * @param  None
  * @retval None.
  */
void UART_C2C_FlushBuffer(void)
{
  memset(UART_RxData.data, 0, RING_BUFFER_SIZE);
  UART_RxData.head = UART_RxData.tail = 0;
}

/**
  * @brief  Send Data to the C2C module over the UART interface.
  *         This function allows sending data to the  C2C Module, the
  *         data can be either an AT command or raw data to send over
  *         a pre-established C2C connection.
  * @param pData: data to send.
  * @param Length: the data length.
  * @retval 0 on success, -1 otherwise.
  */
int16_t UART_C2C_SendData(uint8_t* pData, uint16_t Length)
{
  if (HAL_UART_Transmit(&huart4, (uint8_t*)pData, Length, 2000) != HAL_OK)
  {
     return -1;
  }

  return 0;
}


/**
  * @brief  Retrieve on Data from intermediate IT buffer
  * @param pData: data to send.
  * @retval 0 data available, -1 no data to retrieve
  */
int16_t  UART_C2C_ReceiveSingleData(uint8_t* pSingleData)
{
  /* Note: other possible implementation is to retrieve directly one data from UART buffer */
  /* without using the interrupt and the intermediate buffer */

  if(UART_RxData.head != UART_RxData.tail)
  {
    /* serial data available, so return data to user */
    *pSingleData = UART_RxData.data[UART_RxData.head++];

    /* check for ring buffer wrap */
    if (UART_RxData.head >= RING_BUFFER_SIZE)
    {
      /* ring buffer wrap, so reset head pointer to start of buffer */
      UART_RxData.head = 0;
    }
  }
  else
  {
   return -1;
  }

  return 0;
}


/**
  * @brief  Rx Callback when new data is received on the UART.
  * @param  UartHandle: Uart handle receiving the data.
  * @retval None.
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *UartH)
{
  /* If ring buffer end is reached reset tail pointer to start of buffer */
  if(++UART_RxData.tail >= RING_BUFFER_SIZE)
  {
    UART_RxData.tail = 0;
  }
  if(UART_RxData.tail == UART_RxData.head)
  {
	  ++UART_RxData.head;
	  if (UART_RxData.head >= RING_BUFFER_SIZE)
	  {
		  /* ring buffer wrap, so reset head pointer to start of buffer */
		  UART_RxData.head = 0;
	  }
  }
  HAL_UART_Receive_IT(UartH, (uint8_t *)&UART_RxData.data[UART_RxData.tail], 1);
}

/* Global ETM context struct */
ETMObject_t ETMC2cObj;

void ETM_Run(void){
	ETM_HwStatusInit();

	ETM_RegisterTickCb(&ETMC2cObj, xTaskGetTickCount);
	if(ETM_RegisterBusIO(&ETMC2cObj, UART_C2C_Init, UART_C2C_DeInit, UART_C2C_SetBaudrate, UART_C2C_SendData, UART_C2C_ReceiveSingleData, UART_C2C_FlushBuffer) == ETM_RETURN_OK)
	    configPRINTF(("\r\nStartup complete\r\n"));
	else
		configPRINTF(("\r\nStartup ERROR!\r\n"));

    ETM_HwPowerUp();
}

#endif
