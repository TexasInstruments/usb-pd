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
#include "tps25751_patch_load_from_mcu.h"

/* Functions/Structures for driver development */
static Display_Handle display;
SemaphoreHandle_t xSemaphore;
void interruptEventCallback(uint_least8_t index);
static void drainSemaphoreObject(SemaphoreHandle_t xSemaphore);

/* USB Structures */

/* This register is used as a local copy and to manage
    the INT_EVENT register from the TPS25751 */
static tIntEventRegister curEventRegister;

/* This data structure is used to clear all interrupt flags
    and is initialized to all 0xFFs */
static tIntEventRegister clearAllEventReg;

/* This command is used as inputs to the PBMs 4CC command */
static tPBMDataReg curPBMDataReg = 
{
    .bits.numOfBytes = TPS25751_PBM_DATA_PAYLOAD_SIZE,
    .bits.i2cTargetAddr = TPS25751_BURST_REG,
    .bits.timeoutValue = TPS25751_PBMS_TIMEOUT
};
static tPBMDataReg readPBMDataReg;

/* PBMs 4CC command */
const t4CCCommand pbms4CCCommand = 
{
    .commandRegister = TPS25751_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25751_4CC_PBMs_CMD
};

/* PBMc 4CC command */
const t4CCCommand pbmc4CCCommand = 
{
    .commandRegister = TPS25751_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25751_4CC_PBMc_CMD
};

/* Customer Use Registers */
static tCustomerUseRegister custReg;

/* Local variable for I2C driver communication */
static tTPS25751WriteCommand curWriteCommand;
static tPBMsResponse pbmResp;

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    I2C_Handle i2c;
    I2C_Params i2cParams;
    I2C_Transaction i2cTransaction;
    uint8_t addrReg;
    uint32_t ii;
    uint32_t curBurstPosition;
    uint32_t curBurstSize;
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

    /* Initializing the initial structures */
    memset(curEventRegister.bytes + 1, 0x00, sizeof(tIntEventRegister) - 1);
    curEventRegister.bits.cmd1Complete = 1;
    curEventRegister.bits.rdyForPatch = 1;
    memset(clearAllEventReg.bytes + 1, 0xFF, sizeof(tIntEventRegister) - 1);

    /* Open the UART display for output */
    display = Display_open(Display_Type_UART, NULL);
    if (display == NULL)
    {
        while (1)
        {
        }
    }

    Display_printf(display, 0, 0, "\n--- Patch Load from MCU ---");

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

    /* Waiting for an interrupt and debouncing */
    Display_printf(display, 0, 0, "Waiting for initial interrupt and debouncing...");
    do
    {
        xSemaphoreTake(xSemaphore, portMAX_DELAY);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    } while (GPIO_read(CONFIG_GPIO_PD_IRQ));

    /* Setting the peripheral address */
    i2cTransaction.targetAddress = TPS25751_I2C_TARGET_ADDR;

    /* Waiting for the device to be in PTCH mode  */
    modeReg.mode = 0;
    addrReg = TPS25751_MODE_REG;
    Display_printf(display, 0, 0, "Waiting for device to be in PTCH mode...");
    while (modeReg.mode != TPS25751_MODE_PTCH)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);

        i2cTransaction.writeBuf   = &addrReg;
        i2cTransaction.writeCount = 1;
        i2cTransaction.readBuf    = &modeReg;
        i2cTransaction.readCount  = sizeof(tModeRegister);

        I2C_transfer(i2c, &i2cTransaction);
    }

    /* Setting interrupt mask to enable CMD1 complete and PATCH loaded */
    Display_printf(display, 0, 0, "Enabling CMD1 interrupts...");
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_MASK_REG;
    memcpy(&curWriteCommand.registerData, &curEventRegister.bytes, sizeof(tIntEventRegister));
    i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
    i2cTransaction.writeBuf = &curWriteCommand;
    i2cTransaction.readCount = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Clearing all active interrupts */
    Display_printf(display, 0, 0, "Clearing lingering interrupts...");
    memcpy(&curWriteCommand.registerData, &clearAllEventReg.bytes, sizeof(tIntEventRegister));
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    i2cTransaction.writeBuf = &curWriteCommand;
    i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
    i2cTransaction.readCount = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    drainSemaphoreObject(xSemaphore);

    /* Send PBMs Data */
    Display_printf(display, 0, 0, "Setting PBMs data...");
    curPBMDataReg.bits.lowerRegionSize = gSizeLowRegionArray;
    memcpy(&curWriteCommand.registerData, &curPBMDataReg.bytes, sizeof(tPBMDataReg));
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    i2cTransaction.writeBuf = &curWriteCommand;
    i2cTransaction.writeCount = sizeof(tPBMDataReg) + 1;
    i2cTransaction.readCount = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Reading back the PBMs data to verify correct write */
    addrReg = TPS25751_CMD1_DATA_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &readPBMDataReg;
    i2cTransaction.readCount  = sizeof(tPBMDataReg);

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(memcmp(&readPBMDataReg + 1, &curPBMDataReg + 1, sizeof(tPBMDataReg) - 1) != 0)
    {
        Display_printf(display, 0, 0, "PBM data register failed to write!");
        goto TPS25751ErrorClosure;
    }

    /* Sending PMBs Command */
    Display_printf(display, 0, 0, "Sending PBMs command...");
    i2cTransaction.writeBuf = (void*)&pbms4CCCommand;
    i2cTransaction.writeCount = sizeof(t4CCCommand);
    i2cTransaction.readCount  = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "Error issuing 4CC command\n");
        goto TPS25751ErrorClosure;
    }

    /* Waiting for CMD1 interrupt */
    xSemaphoreTake(xSemaphore, portMAX_DELAY);

    /* Clear CMD1 interrupt*/
    Display_printf(display, 0, 0, "Clearing interrupts...");
    memcpy(&curWriteCommand.registerData, &clearAllEventReg.bytes, sizeof(tIntEventRegister));
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    i2cTransaction.writeBuf = &curWriteCommand;
    i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
    i2cTransaction.readCount = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Reading output data to verify command success */
    addrReg = TPS25751_CMD1_DATA_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &pbmResp;
    i2cTransaction.readCount  = sizeof(tPBMsResponse);

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(pbmResp.bits.status != 0x00)
    {
        Display_printf(display, 0, 0, "Invalid PBMs response 0x%x", pbmResp.bits.status);
        goto TPS25751ErrorClosure;
    }

    /* Sending the image over I2C */
    Display_printf(display, 0, 0, "Sending patch data...");
    i2cTransaction.targetAddress = TPS25751_BURST_REG;
    i2cTransaction.writeBuf = (void*)(tps25751x_lowRegion_i2c_array);
    i2cTransaction.writeCount = gSizeLowRegionArray;
    i2cTransaction.readCount = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Delaying for 50ms */
    vTaskDelay(50 / portTICK_PERIOD_MS);

    /* Send PBMc command to device */
    i2cTransaction.targetAddress = TPS25751_I2C_TARGET_ADDR;
    i2cTransaction.writeBuf = (void*)&pbmc4CCCommand;
    i2cTransaction.writeCount = sizeof(t4CCCommand);
    i2cTransaction.readCount  = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "Error issuing 4CC command\n");
        goto TPS25751ErrorClosure;
    }

    /* Waiting for the interrupt and reading the CMD1 register*/
    xSemaphoreTake(xSemaphore, portMAX_DELAY);

    /* Clear CMD1 interrupt*/
    Display_printf(display, 0, 0, "Clearing CMD1...");

    memcpy(&curWriteCommand.registerData, &clearAllEventReg.bytes, sizeof(tIntEventRegister));
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    i2cTransaction.writeBuf = &curWriteCommand;
    i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
    i2cTransaction.readCount = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Reading the result register from data */
    addrReg = TPS25751_CMD1_DATA_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &pbmResp;
    i2cTransaction.readCount  = sizeof(tPBMsResponse);

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(pbmResp.bits.status != 0x00)
    {
        Display_printf(display, 0, 0, "PBMc command failed to send!");
        goto TPS25751ErrorClosure;
    }

    /* Reading the MODE register to verify we are now in APP  mode */
    Display_printf(display, 0, 0, "Waiting for device to be in APP mode...");
    addrReg = TPS25751_MODE_REG;
    modeReg.mode = 0;  
    while (modeReg.mode != TPS25751_MODE_APP)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);

        i2cTransaction.writeBuf   = &addrReg;
        i2cTransaction.writeCount = 1;
        i2cTransaction.readBuf    = &modeReg;
        i2cTransaction.readCount  = sizeof(tModeRegister);

         if (I2C_transfer(i2c, &i2cTransaction) == false)
        {
            Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
            goto TPS25751ErrorClosure;
        }
    }

    Display_printf(display, 0, 0, "Device is in APP, patch loaded!");

    /* Reading customer use register 1 */
    Display_printf(display, 0, 0, "Reading customer user register...");
    addrReg = TPS25751_CUST_USE_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &custReg;
    i2cTransaction.readCount  = sizeof(tCustomerUseRegister);

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if((custReg.custRegWord2 != 0xCAFEBEEF) || (custReg.custRegWord1 != 0xDEADBEEF))
    {
        Display_printf(display, 0, 0, "ERROR! ustomer user registers did not match!");
        goto TPS25751ErrorClosure;
    }
    else
    {
        Display_printf(display, 0, 0, "Customer use registers matched!");
        Display_printf(display, 0, 0, "Device flashed successfully!");
    }

TPS25751ErrorClosure:
    I2C_close(i2c);
    Display_printf(display, 0, 0, "I2C closed!");
    return (NULL);
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
