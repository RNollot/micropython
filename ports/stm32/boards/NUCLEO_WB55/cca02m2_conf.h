/**
 ******************************************************************************
 * @file    cca02m2_conf.h
 * @author  SRA - Central Labs
 * @version v1.0.0
 * @date    06-Dec-19
 * @brief   This file contains definitions for the MEMSMIC1 applications
******************************************************************************
* @attention
*
* <h2><center>&copy; Copyright (c) 2019 STMicroelectronics. 
* All rights reserved.</center></h2>
*
* This software component is licensed by ST under BSD 3-Clause license,
* the "License"; You may not use this file except in compliance with the 
* License. You may obtain a copy of the License at:
*                        opensource.org/licenses/BSD-3-Clause
*
******************************************************************************
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef CCA02M2_CONF_H__
#define CCA02M2_CONF_H__

#ifdef __cplusplus
extern "C" {
#endif
  
/* Includes ------------------------------------------------------------------*/  
#include "stm32wbxx_hal.h"
#include "nucleo_wb55rg_bus.h"
#include "nucleo_wb55rg_errno.h"


/* The default value of the N_MS_PER_INTERRUPT directive in the driver is set to 1, 
for backward compatibility: leaving this values as it is allows to avoid any 
modification in the application layer developed with the older versions of the driver */

/*Number of millisecond of audio at each DMA interrupt*/
#define N_MS_PER_INTERRUPT               (10U)
  
#define AUDIO_IN_CHANNELS 				2
#define AUDIO_IN_SAMPLING_FREQUENCY 	16000

#define AUDIO_IN_BUFFER_SIZE            DEFAULT_AUDIO_IN_BUFFER_SIZE  
#define AUDIO_VOLUME_INPUT              16U
#define CCA02M2_AUDIO_INSTANCE 			0U
#define CCA02M2_AUDIO_IN_IT_PRIORITY    0U

#if (AUDIO_IN_SAMPLING_FREQUENCY == 8000)
#define MAX_DECIMATION_FACTOR 160
#else
#define MAX_DECIMATION_FACTOR 128
#endif
  
  /*#define USE_SPI3*/
  /*If you wanto to use SPI3 instead of SPI2 for M3 and M4, uncomment this define and 
  close SB20 and SB21*/

#ifdef __cplusplus
}
#endif

#endif /* CCA02M2_CONF_H__*/

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

