/*
 * Copyright (c) 2026, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Driver Header files */
#include <ti/display/DisplayUart.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/I2C.h>

#include "tps_usbpd_i2c_driver.h"
#include "ti_drivers_config.h"

#include <FreeRTOS.h>
#include <semphr.h>

/* Driver Objects */
static I2C_Handle i2c;
static I2C_Params i2cParams;
static Display_Handle display;
static uint8_t i2cTargetAddr;

/* RTOS Handles */
static void interruptEventCallback(uint_least8_t index);
static void drainSemaphoreObject(SemaphoreHandle_t xSemaphore);
static SemaphoreHandle_t xSemaphore;


bool TPS_USBPD_i2cTransfer(void* writeData, size_t writeLen,
                            void* readData, size_t readLen)
{
    I2C_Transaction i2cTransaction;
    i2cTransaction.targetAddress = i2cTargetAddr;

    i2cTransaction.readBuf = readData;
    i2cTransaction.readCount = readLen;
    i2cTransaction.writeBuf = writeData;  
    i2cTransaction.writeCount = writeLen;

    if((i2cTransaction.writeCount == 0) && (i2cTransaction.readCount == 0))
    {
        return (false);
    }

    return (I2C_transfer(i2c, &i2cTransaction));
}

extern void TPS_USBPD_setI2CTargetAddress(uint8_t addr)
{
    i2cTargetAddr = addr;
}

void TPS_USBPD_logMessage(const char *format, ...)
{
    va_list args;

    va_start(args, format);

    if (display != NULL)
    {
        display->fxnTablePtr->vprintfFxn(display, 0, 0, format, args);
    }

    va_end(args);
}

void TPS_USBPD_delayMS(uint32_t delayInMS)
{
    vTaskDelay(delayInMS / portTICK_PERIOD_MS);
}

bool TPS_USBPD_pendOnIRQ(uint32_t timeoutValue)
{
    if(xSemaphoreTake(xSemaphore, timeoutValue) == pdTRUE)
    {
        return (true);
    }

    return (false);
}

extern bool TPS_USBPD_initializeDevice(void)
{
    Display_init();
    I2C_init();
    GPIO_init();

    /* Configuring the GPIO input interrupt */
    GPIO_setConfig(CONFIG_GPIO_PD_IRQ, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING | CONFIG_GPIO_PD_IRQ_IOMUX);
    GPIO_setCallback(CONFIG_GPIO_PD_IRQ, interruptEventCallback);
    GPIO_enableInt(CONFIG_GPIO_PD_IRQ);

     /* Open the UART display for output */
    display = Display_open(Display_Type_UART, NULL);
    if (display == NULL)
    {
        return (false);
    }

    /* Create I2C for usage */
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_100kHz;
    i2c = I2C_open(CONFIG_I2C_TMP, &i2cParams);
    if (i2c == NULL)
    {
        TPS_USBPD_logMessage("Error Initializing I2C!");
        return (false);
    }

    TPS_USBPD_setI2CTargetAddress(TPS_USBPD_I2C_DEFAULT_TARGET_ADDR);

    xSemaphore = xSemaphoreCreateCounting(1,0);

    if(xSemaphore == NULL)
    {
        return (false);
    }

    drainSemaphoreObject(xSemaphore);

    return (true);
}

extern void TPS_USBPD_closeDevice(void)
{
    I2C_close(i2c);
    TPS_USBPD_logMessage("I2C closed!");
}

static void drainSemaphoreObject(SemaphoreHandle_t xSemaphore)
{
    while(xSemaphoreTake(xSemaphore, 0) == pdTRUE)
    {
        // Keep taking until no more tokens are available
    }
}

static void interruptEventCallback(uint_least8_t index)
{
    xSemaphoreGiveFromISR(xSemaphore, NULL);
}
