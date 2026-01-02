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

/* I2C target addresses */
const uint8_t i2cTargetAddr = 0x21;

/* Functions/Structures for driver development */
static Display_Handle display;
SemaphoreHandle_t xSemaphore;
void interruptEventCallback(uint_least8_t index);

/* USB Structures */
tIntEventRegister curEventRegister;
tBootFlagsRegister curBootFlagRegister;
tTPS25751WriteCommand curWriteCommand;
const t4CCCommand deadBatteryClearCommand = 
{
    .commandRegister = TPS25751_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25751_4CC_DBfg_CMD
};

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    I2C_Handle i2c;
    I2C_Params i2cParams;
    I2C_Transaction i2cTransaction;
    uint8_t addrReg;

    /* Call driver init functions and create RTOS objects */
    Display_init();
    I2C_init();
    GPIO_init();
    xSemaphore = xSemaphoreCreateCounting(1,0);

    /* Configuring the GPIO input interrupt */
    GPIO_setConfig(CONFIG_GPIO_PD_IRQ, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING | CONFIG_GPIO_PD_IRQ_IOMUX);
    GPIO_setCallback(CONFIG_GPIO_PD_IRQ, interruptEventCallback);
    GPIO_enableInt(CONFIG_GPIO_PD_IRQ);

    /* Open the UART display for output */
    display = Display_open(Display_Type_UART, NULL);
    if (display == NULL)
    {
        while (1)
        {
        }
    }

    Display_printf(display, 0, 0, "\n--- TPS25751 Dead Battery Code Example ---");

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
    } else
    {
        Display_printf(display, 0, 0, "I2C Initialized!");
    }

    /* Setting the peripheral address */
    i2cTransaction.targetAddress = i2cTargetAddr;

DEAD_BATTERY_TASK_START:

    /* Waiting for the interrupt event */
    Display_printf(display, 0, 0, "Waiting for I2C interrupt...");
    xSemaphoreTake(xSemaphore, portMAX_DELAY);

    /* Setting up the read transaction to the event register */
    addrReg = TPS25751_INT_EVENT_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &curEventRegister;
    i2cTransaction.readCount  = sizeof(tIntEventRegister);

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto DEAD_BATTERY_TASK_START;
    }

    /* Seeing if there was a plug event  */
    if(curEventRegister.plugInsertRemoval == 1)
    {
        Display_printf(display, 0, 0, "Plug event detected! Clearing flag.");

        /* If there is a plug event, clear the plug event flag */
        curEventRegister.plugInsertRemoval = 0;
        curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
        memcpy(&curWriteCommand.registerData, &curEventRegister, sizeof(tIntEventRegister));
        i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
        i2cTransaction.readCount = 0;
        
        if (I2C_transfer(i2c, &i2cTransaction) == false)
        {
            Display_printf(display, 0, 0, "Error clearing interrupt event registers!");
            goto DEAD_BATTERY_TASK_START;
        }

        Display_printf(display, 0, 0, "Reading boot flags register...");
        addrReg = TPS25751_BOOT_FLAGS_REG;
        i2cTransaction.writeBuf   = &addrReg;
        i2cTransaction.writeCount = 1;
        i2cTransaction.readBuf    = &curBootFlagRegister;
        i2cTransaction.readCount  = sizeof(tBootFlagsRegister);

        if (I2C_transfer(i2c, &i2cTransaction) == false)
        {
            Display_printf(display, 0, 0, "Error reading boot flag registers!");
            goto DEAD_BATTERY_TASK_START;
        }

        if(curBootFlagRegister.deadBatteryFlag == 1)
        {
            Display_printf(display, 0, 0, "Dead battery flag detected!");
        }
        else
        {
            Display_printf(display, 0, 0, "Dead battery flag not detected!");
            goto DEAD_BATTERY_TASK_START;
        }

        /* Issuing DBfg command */
        Display_printf(display, 0, 0, "Issuing DBfg 4CC command");

        i2cTransaction.writeBuf = (void*)&deadBatteryClearCommand;
        i2cTransaction.writeCount = sizeof(t4CCCommand);
        i2cTransaction.readCount  = 0;

        if (I2C_transfer(i2c, &i2cTransaction) == false)
        {
            Display_printf(display, 0, 0, "Error issuing 4CC command\n");
            goto DEAD_BATTERY_TASK_START;
        }

        /* Otherwise, sleep for a bit and read back the boot register to confirm dead battery
            flag was cleared */
        vTaskDelay(500 / portTICK_PERIOD_MS);
        addrReg = TPS25751_BOOT_FLAGS_REG;
        i2cTransaction.writeBuf   = &addrReg;
        i2cTransaction.writeCount = 1;
        i2cTransaction.readBuf    = &curBootFlagRegister;
        i2cTransaction.readCount  = sizeof(tBootFlagsRegister);

        if (I2C_transfer(i2c, &i2cTransaction) == false)
        {
            Display_printf(display, 0, 0, "Error reading boot flag registers!");
            goto DEAD_BATTERY_TASK_START;
        }

        if(curBootFlagRegister.deadBatteryFlag == 0)
        {
            /* Set a breakpoint here to demonstrate functionality. */
            Display_printf(display, 0, 0, "Dead battery flag cleared successfully!");
            __NOP();
        }
        else
        {
            Display_printf(display, 0, 0, "Dead battery flag not cleared!");
            goto DEAD_BATTERY_TASK_START;
        }
    }
    else
    {
        goto DEAD_BATTERY_TASK_START;
    }

    I2C_close(i2c);
    Display_printf(display, 0, 0, "I2C closed!");
    return (NULL);
}

void interruptEventCallback(uint_least8_t index)
{
    xSemaphoreGiveFromISR(xSemaphore, NULL);
}
