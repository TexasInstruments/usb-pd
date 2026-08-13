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
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

/* Driver Header files */
#include <ti/display/DisplayUart.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/I2C.h>

/* Driver configuration */
#include "ti_drivers_config.h"
#include <FreeRTOS.h>
#include <semphr.h>

/* USB Configuration */
#include "tps25751.h"

/* Functions/Structures for driver development */
static Display_Handle display;
SemaphoreHandle_t xSemaphore;
void interruptEventCallback(uint_least8_t index);
static void drainSemaphoreObject(SemaphoreHandle_t xSemaphore);
static void printInterruptStatus(tIntEventRegister* intEvent, Display_Handle display);

 #define TPS25751_I2C_TARGET_ADDR     0x21

/* USB Structures */

/* Event register to read currently triggered flags */
static tIntEventRegister curTriggeredIntsReg;

/* Local variable for I2C driver communication */
static tTPS25751WriteCommand curWriteCommand;
/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    I2C_Handle i2c;
    I2C_Params i2cParams;
    I2C_Transaction i2cTransaction;
    uint8_t addrReg;
    uint8_t sizeWritten;
    uint32_t ii;
    tModeRegister modeReg;

    /* Call driver init functions and create RTOS objects */
    Display_init();
    I2C_init();
    GPIO_init();
    xSemaphore = xSemaphoreCreateCounting(1,0);

    /* Configuring the GPIO input interrupt */
    GPIO_setConfig(CONFIG_GPIO_PD_IRQ, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING | CONFIG_GPIO_PD_IRQ_IOMUX);
    GPIO_setCallback(CONFIG_GPIO_PD_IRQ, interruptEventCallback);
    GPIO_enableInt(CONFIG_GPIO_PD_IRQ);

    /* Making sure interrupt is not pended */
    drainSemaphoreObject(xSemaphore);

     /* Open the UART display for output */
    display = Display_open(Display_Type_UART, NULL);
    if (display == NULL)
    {
        while (1)
        {
        }
    }

    Display_printf(display, 0, 0, "\n--- TPS25751A Interrupt Monitor ---");

    /* Create I2C for usage */
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_100kHz;
    i2c = I2C_open(CONFIG_I2C_TMP, &i2cParams);
    if (i2c == NULL)
    {
        Display_printf(display, 0, 0, "Error Initializing I2C!");
        while (1)
        {
        }
    }
    else
    {
        Display_printf(display, 0, 0, "I2C Initialized!");
    }

    /* Setting the peripheral address */
    i2cTransaction.targetAddress = TPS25751_I2C_TARGET_ADDR;

    /* Waiting for the device to be in APP mode  */
    modeReg.mode = 0;
    addrReg = TPS25751_MODE_REG;
    Display_printf(display, 0, 0, "Waiting for device to be in APP mode...");
    while (modeReg.mode != TPS25751_MODE_APP)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);

        i2cTransaction.writeBuf   = &addrReg;
        i2cTransaction.writeCount = 1;
        i2cTransaction.readBuf    = &modeReg;
        i2cTransaction.readCount  = sizeof(tModeRegister);

        I2C_transfer(i2c, &i2cTransaction);
    }

    do
    {
        /* Reading out interrupt */
        addrReg = TPS25751_INT_EVENT_REG;
        i2cTransaction.writeBuf = &addrReg;
        i2cTransaction.writeCount = 1;
        i2cTransaction.readCount = sizeof(tIntEventRegister);
        i2cTransaction.readBuf = &curTriggeredIntsReg;

        if (I2C_transfer(i2c, &i2cTransaction) == false)
        {
            /* In this case we NAKed. Normally we would fault out here, but it is possible 
                that the device just isn't powered up so let's delay for 100ms and then
                try again */
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        /* Clearing the interrupts */
        memcpy(&curWriteCommand.registerData, &curTriggeredIntsReg.bytes, sizeof(tIntEventRegister));
        curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
        i2cTransaction.writeBuf = &curWriteCommand;
        i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
        i2cTransaction.readCount = 0;

        if (I2C_transfer(i2c, &i2cTransaction) == false)
        {
            Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
            goto TPS25751ErrorClosure;
        }

        /* Dumping the relevant information to the terminal */
        printInterruptStatus(&curTriggeredIntsReg, display);

        Display_printf(display, 0, 0, "\nPending on I2Ct_IRQ...");
        xSemaphoreTake(xSemaphore, portMAX_DELAY);

    } while(1);
    
    Display_printf(display, 0, 0, "\nPending on I2Ct_IRQ...");

    
TPS25751ErrorClosure:
    I2C_close(i2c);
    Display_printf(display, 0, 0, "I2C closed!");
    return (NULL);
}

static void printInterruptStatus(tIntEventRegister* intEvent, Display_Handle display)
{
    Display_printf(display, 0, 0, "--- INT_EVENT ---");
    Display_printf(display, 0, 0, "   pdHardReset = %x", intEvent->bits.pdHardReset);
    Display_printf(display, 0, 0, "   plugInsertRemoval = %x", intEvent->bits.plugInsertRemoval);
    Display_printf(display, 0, 0, "   powerSwapComplete = %x", intEvent->bits.powerSwapComplete);
    Display_printf(display, 0, 0, "   dataSwapComplete = %x", intEvent->bits.dataSwapComplete);
    Display_printf(display, 0, 0, "   overcurrent = %x", intEvent->bits.overcurrent);
    Display_printf(display, 0, 0, "   newContractCons = %x", intEvent->bits.newContractCons);
    Display_printf(display, 0, 0, "   newContractProv = %x", intEvent->bits.newContractProv);
    Display_printf(display, 0, 0, "   sourceCapRec = %x", intEvent->bits.sourceCapRec);
    Display_printf(display, 0, 0, "   sinkCapRec = %x", intEvent->bits.sinkCapRec);
    Display_printf(display, 0, 0, "   powerSwapReq = %x", intEvent->bits.powerSwapReq);
    Display_printf(display, 0, 0, "   dataswapReq = %x", intEvent->bits.dataswapReq);
    Display_printf(display, 0, 0, "   usbHostPresent = %x", intEvent->bits.usbHostPresent);
    Display_printf(display, 0, 0, "   usbHostNotPresent = %x", intEvent->bits.usbHostNotPresent);
    Display_printf(display, 0, 0, "   pwrPathSwChanged = %x", intEvent->bits.pwrPathSwChanged);
    Display_printf(display, 0, 0, "   powerStatUpdate = %x", intEvent->bits.powerStatUpdate);
    Display_printf(display, 0, 0, "   statusUpdate = %x", intEvent->bits.statusUpdate);
    Display_printf(display, 0, 0, "   pdStatusUpdate = %x", intEvent->bits.pdStatusUpdate);
    Display_printf(display, 0, 0, "   cmd1Complete = %x", intEvent->bits.pdStatusUpdate);
    Display_printf(display, 0, 0, "   devIncompError = %x", intEvent->bits.devIncompError);
    Display_printf(display, 0, 0, "   cannotProvVolCur = %x", intEvent->bits.cannotProvVolCur);
    Display_printf(display, 0, 0, "   canProvVolCurLtr = %x", intEvent->bits.canProvVolCurLtr);
    Display_printf(display, 0, 0, "   powerEvent = %x", intEvent->bits.powerEvent);
    Display_printf(display, 0, 0, "   missingGetCaps = %x", intEvent->bits.missingGetCaps);
    Display_printf(display, 0, 0, "   protocolError = %x", intEvent->bits.protocolError);
    Display_printf(display, 0, 0, "   msgDataError = %x", intEvent->bits.msgDataError);
    Display_printf(display, 0, 0, "   sinkTransComplete = %x", intEvent->bits.sinkTransComplete);
    Display_printf(display, 0, 0, "   plugEarlyNotf = %x", intEvent->bits.plugEarlyNotf);
    Display_printf(display, 0, 0, "   unableToSource = %x", intEvent->bits.unableToSource);
    Display_printf(display, 0, 0, "   extDCDCSinkSafe = %x", intEvent->bits.extDCDCSinkSafe);
    Display_printf(display, 0, 0, "   extDCDCSourceSafe = %x", intEvent->bits.extDCDCSourceSafe);
    Display_printf(display, 0, 0, "   liquidDetect = %x", intEvent->bits.liquidDetect);
    Display_printf(display, 0, 0, "   txMemBuffEmpty = %x", intEvent->bits.txMemBuffEmpty);
    Display_printf(display, 0, 0, "   mbrdBuffReady = %x", intEvent->bits.mbrdBuffReady);
    Display_printf(display, 0, 0, "   patchLoaded = %x", intEvent->bits.patchLoaded);
    Display_printf(display, 0, 0, "   rdyForPatch = %x", intEvent->bits.rdyForPatch);
    Display_printf(display, 0, 0, "   i2cConNacked = %x", intEvent->bits.i2cConNacked);
}

void interruptEventCallback(uint_least8_t index)
{
    xSemaphoreGiveFromISR(xSemaphore, NULL);
}

static void drainSemaphoreObject(SemaphoreHandle_t xSemaphore)
{
    while(xSemaphoreTake(xSemaphore, 0) == pdTRUE)
    {
        // Keep taking until no more tokens are available
    }
}
