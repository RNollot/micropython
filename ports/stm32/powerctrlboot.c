/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018-2019 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "py/mphal.h"
#include "irq.h"
#include "powerctrl.h"

static inline void powerctrl_config_systick(void) {
    // Configure SYSTICK to run at 1kHz (1ms interval)
    SysTick->CTRL |= SYSTICK_CLKSOURCE_HCLK;
    SysTick_Config(HAL_RCC_GetHCLKFreq() / 1000);
    NVIC_SetPriority(SysTick_IRQn, IRQ_PRI_SYSTICK);
}

#if defined(STM32F0)

void SystemClock_Config(void) {
    // Enable power control peripheral
    __HAL_RCC_PWR_CLK_ENABLE();

    // Set flash latency to 1 because SYSCLK > 24MHz
    FLASH->ACR = (FLASH->ACR & ~0x7) | 0x1;

    #if MICROPY_HW_CLK_USE_HSI48
    // Use the 48MHz internal oscillator
    // HAL does not support RCC CFGR SW=3 (HSI48 direct to SYSCLK)
    // so use HSI48 -> PREDIV(divide by 2) -> PLL (mult by 2) -> SYSCLK.

    RCC->CR2 |= RCC_CR2_HSI48ON;
    while ((RCC->CR2 & RCC_CR2_HSI48RDY) == 0) {
        // Wait for HSI48 to be ready
    }
    RCC->CFGR = 0 << RCC_CFGR_PLLMUL_Pos | 3 << RCC_CFGR_PLLSRC_Pos; // PLL mult by 2, src = HSI48/PREDIV
    RCC->CFGR2 = 1; // Input clock divided by 2

    #elif MICROPY_HW_CLK_USE_HSE
    // Use HSE and the PLL to get a 48MHz SYSCLK

    #if MICROPY_HW_CLK_USE_BYPASS
    RCC->CR |= RCC_CR_HSEBYP;
    #endif
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0) {
        // Wait for HSE to be ready
    }
    RCC->CFGR = ((48000000 / HSE_VALUE) - 2) << RCC_CFGR_PLLMUL_Pos | 2 << RCC_CFGR_PLLSRC_Pos;
    RCC->CFGR2 = 0; // Input clock not divided

    #elif MICROPY_HW_CLK_USE_HSI
    // Use the 8MHz internal oscillator and the PLL to get a 48MHz SYSCLK

    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0) {
        // Wait for HSI to be ready
    }
    RCC->CFGR = 4 << RCC_CFGR_PLLMUL_Pos | 1 << RCC_CFGR_PLLSRC_Pos; // PLL mult by 6, src = HSI
    RCC->CFGR2 = 0; // Input clock not divided

    #else
    #error System clock not specified
    #endif

    RCC->CR |= RCC_CR_PLLON; // Turn PLL on
    while ((RCC->CR & RCC_CR_PLLRDY) == 0) {
        // Wait for PLL to lock
    }
    const uint32_t sysclk_src = 2;

    // Select SYSCLK source
    RCC->CFGR |= sysclk_src << RCC_CFGR_SW_Pos;
    while (((RCC->CFGR >> RCC_CFGR_SWS_Pos) & 0x3) != sysclk_src) {
        // Wait for SYSCLK source to change
    }

    SystemCoreClockUpdate();
    powerctrl_config_systick();
}

#elif defined(STM32L0)

void SystemClock_Config(void) {
    // Enable power control peripheral
    __HAL_RCC_PWR_CLK_ENABLE();

    // Set flash latency to 1 because SYSCLK > 16MHz
    FLASH->ACR |= FLASH_ACR_LATENCY;

    // Enable the 16MHz internal oscillator
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {
    }

    // Use HSI16 and the PLL to get a 32MHz SYSCLK
    RCC->CFGR = 1 << RCC_CFGR_PLLDIV_Pos | 1 << RCC_CFGR_PLLMUL_Pos;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {
        // Wait for PLL to lock
    }
    const uint32_t sysclk_src = 3;

    // Select SYSCLK source
    RCC->CFGR |= sysclk_src << RCC_CFGR_SW_Pos;
    while (((RCC->CFGR >> RCC_CFGR_SWS_Pos) & 0x3) != sysclk_src) {
        // Wait for SYSCLK source to change
    }

    SystemCoreClockUpdate();
    powerctrl_config_systick();

    #if MICROPY_HW_ENABLE_RNG || MICROPY_HW_ENABLE_USB
    // Enable the 48MHz internal oscillator
    RCC->CRRCR |= RCC_CRRCR_HSI48ON;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    SYSCFG->CFGR3 |= SYSCFG_CFGR3_ENREF_HSI48;
    while (!(RCC->CRRCR & RCC_CRRCR_HSI48RDY)) {
        // Wait for HSI48 to be ready
    }

    // Select RC48 as HSI48 for USB and RNG
    RCC->CCIPR |= RCC_CCIPR_HSI48SEL;

    #if MICROPY_HW_ENABLE_USB
    // Synchronise HSI48 with 1kHz USB SoF
    __HAL_RCC_CRS_CLK_ENABLE();
    CRS->CR = 0x20 << CRS_CR_TRIM_Pos;
    CRS->CFGR = 2 << CRS_CFGR_SYNCSRC_Pos | 0x22 << CRS_CFGR_FELIM_Pos
        | __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000) << CRS_CFGR_RELOAD_Pos;
    #endif
    #endif

}

#elif defined(STM32WB)

#include "stm32wbxx_ll_hsem.h"

// This semaphore protected access to the CLK48 configuration.
// CPU1 should hold this semaphore while the USB peripheral is in use.
// See AN5289 and https://github.com/micropython/micropython/issues/6316.
#define CLK48_SEMID (5)

void SystemClock_Config(void) {
    // Enable the 32MHz external oscillator

#if AUDIO_ENABLE==1

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    /** Configure the main internal regulator output voltage 
    */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    /** Initializes the CPU, AHB and APB busses clocks 
    */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI
                                        |RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV8;
    RCC_OscInitStruct.PLL.PLLN = 32;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
    }
    /** Configure the SYSCLKSource, HCLK, PCLK1 and PCLK2 clocks dividers 
    */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK4|RCC_CLOCKTYPE_HCLK2
                  |RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                  |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.AHBCLK2Divider = RCC_SYSCLK_DIV2;
    RCC_ClkInitStruct.AHBCLK4Divider = RCC_SYSCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
    }

    PeriphClkInitStruct.PeriphClockSelection    = RCC_PERIPHCLK_ADC \
                                        | RCC_PERIPHCLK_SMPS \
                                        | RCC_PERIPHCLK_SAI1 \
                                        | RCC_PERIPHCLK_USB;

    PeriphClkInitStruct.PLLSAI1.PLLN = 86;
    PeriphClkInitStruct.PLLSAI1.PLLP = RCC_PLLP_DIV7;
    PeriphClkInitStruct.PLLSAI1.PLLQ = RCC_PLLQ_DIV2;
    PeriphClkInitStruct.PLLSAI1.PLLR = RCC_PLLR_DIV2;
    PeriphClkInitStruct.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_SAI1CLK;
    PeriphClkInitStruct.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLLSAI1;
    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    PeriphClkInitStruct.SmpsClockSelection = RCC_SMPSCLKSOURCE_HSE;
    PeriphClkInitStruct.SmpsDivSelection = RCC_SMPSCLKDIV_RANGE0;
    PeriphClkInitStruct.AdcClockSelection       = RCC_ADCCLKSOURCE_SYSCLK;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
    }    

    HAL_PWREx_EnableVddUSB();
#else
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {
    }

    // Prevent CPU2 from disabling CLK48.
    while (LL_HSEM_1StepLock(HSEM, CLK48_SEMID)) {
    }

    // Use HSE and the PLL to get a 64MHz SYSCLK
    #define PLLM (HSE_VALUE / 8000000) // VCO input is 8MHz
    #define PLLN (24) // 24*8MHz = 192MHz
    #define PLLQ (4) // f_Q = 48MHz
    #define PLLR (3) // f_R = 64MHz
    RCC->PLLCFGR =
        (PLLR - 1) << RCC_PLLCFGR_PLLR_Pos | RCC_PLLCFGR_PLLREN
            | (PLLQ - 1) << RCC_PLLCFGR_PLLQ_Pos | RCC_PLLCFGR_PLLQEN
            | PLLN << RCC_PLLCFGR_PLLN_Pos
            | (PLLM - 1) << RCC_PLLCFGR_PLLM_Pos
            | 3 << RCC_PLLCFGR_PLLSRC_Pos;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {
        // Wait for PLL to lock
    }
    const uint32_t sysclk_src = 3;

    // Set divider for HCLK2 to 2 so f_HCLK2 = 32MHz
    RCC->EXTCFGR = 8 << RCC_EXTCFGR_C2HPRE_Pos;

    // Set flash latency to 3 because SYSCLK > 54MHz
    FLASH->ACR |= 3 << FLASH_ACR_LATENCY_Pos;

    // Select SYSCLK source
    RCC->CFGR |= sysclk_src << RCC_CFGR_SW_Pos;
    while (((RCC->CFGR >> RCC_CFGR_SWS_Pos) & 0x3) != sysclk_src) {
        // Wait for SYSCLK source to change
    }

    // Select PLLQ as 48MHz source for USB and RNG
    RCC->CCIPR = 2 << RCC_CCIPR_CLK48SEL_Pos;
#endif
    SystemCoreClockUpdate();
    powerctrl_config_systick();


}

#endif
