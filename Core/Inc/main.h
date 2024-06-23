/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "list.h"
#include "defines.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */
#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif
/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
enum page{FS,MOUNT,MENU,IMAGE};
enum action{NONE,IMG_MOUNT,FSDISP};
enum STATUS{RET_OK,RET_ERR};

void EnableTiming(void);
void dumpBuf(unsigned char * buf,long memoryAddr,int len);
char *byte_to_binary(int x);

enum STATUS cmd17GetDataBlockBareMetal(long memoryAdr,unsigned char *buffer);
enum STATUS cmd18GetDataBlocksBareMetal(long memoryAdr,unsigned char * buffer,int count);

// HAL Interrupt function

void HAL_SPI_TxHalfCpltCallback(SPI_HandleTypeDef *hspi);           // DMA SPI Half buffer completion
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi);               // DMA SPI Full buffer completion
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);        // Debouncing button timer interrupt

list_t * sortLinkedList(list_t * plst);                             // Sort the chainedList
enum STATUS walkDir(char * path);                                   // Build chainedList of Directories/Files Items

enum STATUS mountImagefile(char * filename);

int isDiskIIDisable();
void processBtnInterrupt(uint16_t GPIO_Pin);

void processPrevFSItem();
void processNextFSItem();
void processSelectFSItem();

void processToogleOption();
void processMountOption();
void nothing();
void processBtnRet();

enum STATUS swithPage(enum page newPage,void * arg);
void processDiskHeadMove(uint16_t GPIO_Pin);

enum STATUS mountImagefile(char * filename);
enum STATUS initeDMABuffering();

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define BTN_ENTR_Pin GPIO_PIN_13
#define BTN_ENTR_GPIO_Port GPIOC
#define BTN_ENTR_EXTI_IRQn EXTI15_10_IRQn
#define BTN_UP_Pin GPIO_PIN_14
#define BTN_UP_GPIO_Port GPIOC
#define BTN_UP_EXTI_IRQn EXTI15_10_IRQn
#define BTN_DOWN_Pin GPIO_PIN_15
#define BTN_DOWN_GPIO_Port GPIOC
#define BTN_DOWN_EXTI_IRQn EXTI15_10_IRQn
#define STEP0_Pin GPIO_PIN_0
#define STEP0_GPIO_Port GPIOA
#define STEP0_EXTI_IRQn EXTI0_IRQn
#define STEP1_Pin GPIO_PIN_1
#define STEP1_GPIO_Port GPIOA
#define STEP1_EXTI_IRQn EXTI1_IRQn
#define STEP2_Pin GPIO_PIN_2
#define STEP2_GPIO_Port GPIOA
#define STEP2_EXTI_IRQn EXTI2_IRQn
#define STEP3_Pin GPIO_PIN_3
#define STEP3_GPIO_Port GPIOA
#define STEP3_EXTI_IRQn EXTI3_IRQn
#define DEVICE_ENABLE_Pin GPIO_PIN_4
#define DEVICE_ENABLE_GPIO_Port GPIOA
#define SD_EJECT_Pin GPIO_PIN_7
#define SD_EJECT_GPIO_Port GPIOA
#define SD_EJECT_EXTI_IRQn EXTI9_5_IRQn
#define WR_PROTECT_Pin GPIO_PIN_1
#define WR_PROTECT_GPIO_Port GPIOB
#define BTN_RET_Pin GPIO_PIN_10
#define BTN_RET_GPIO_Port GPIOB
#define BTN_RET_EXTI_IRQn EXTI15_10_IRQn
#define WR_REQ_Pin GPIO_PIN_11
#define WR_REQ_GPIO_Port GPIOA
#define WR_REQ_EXTI_IRQn EXTI15_10_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
