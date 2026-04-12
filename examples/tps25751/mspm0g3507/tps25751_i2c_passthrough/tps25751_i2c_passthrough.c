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
#include "tps25751_i2c_passthrough.h"

/* Functions/Structures for driver development */
static Display_Handle display;
SemaphoreHandle_t xSemaphore;
void interruptEventCallback(uint_least8_t index);
static void drainSemaphoreObject(SemaphoreHandle_t xSemaphore);

/* Buffers used for I2C transaction */
static uint8_t curI2CBuffer[TPS25751_I2C_PAYLOAD_SIZE] = 
{
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBE, 0xEF, 0xBA, 0xBE
};

/* Stores the read response */
static uint8_t curI2CReadBuffer[TPS25751_I2C_PAYLOAD_SIZE];


/* This data structure is used to clear all interrupt flags
    and is initialized to all 0xFFs */
static tIntEventRegister clearAllEventReg;

/* 4CC commands for the reads and writes*/
const t4CCCommand i2cWriteCommand = 
{
    .commandRegister = TPS25751_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25751_4CC_I2Cw_CMD
};

static tI2CwDataReg curI2CWrite =
{
    .bits.numOfBytes = (TPS25751_I2CW_DATA_PAYLOAD_SIZE-1)
};

const t4CCCommand i2cReadCommand = 
{
    .commandRegister = TPS25751_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25751_4CC_I2Cr_CMD
};

static tI2CrDataReg curI2CRead =
{
    .bits.numOfBytes = (TPS25751_I2CR_DATA_PAYLOAD_SIZE-1)
};

/* Static variables */
static tModeRegister modeReg;
static tI2CrRespReg  i2cReadRespReg;
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
    uint32_t ii;

    /* Call driver init functions and create RTOS objects */
    Display_init();
    I2C_init();
    GPIO_init();
    xSemaphore = xSemaphoreCreateCounting(1,0);

    /* Configuring the GPIO input interrupt */
    GPIO_setConfig(CONFIG_GPIO_PD_IRQ, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING | CONFIG_GPIO_PD_IRQ_IOMUX);
    GPIO_setCallback(CONFIG_GPIO_PD_IRQ, interruptEventCallback);
    GPIO_enableInt(CONFIG_GPIO_PD_IRQ);

    /* Initializing the initial structures. We are assuming only the CMD1 complete
        interrupt is enabled as this is what we configured in the device example
        configuration  */
    memset(curI2CWrite.bytes + 1, 0x00, sizeof(tI2CwDataReg) - 1);
    memset(clearAllEventReg.bytes + 1, 0xFF, sizeof(tIntEventRegister) - 1);

    /* Open the UART display for output */
    display = Display_open(Display_Type_UART, NULL);
    if (display == NULL)
    {
        while (1)
        {
        }
    }

    Display_printf(display, 0, 0, "\n--- I2C Passthrough Example ---");

    /* Create I2C for usage */
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;
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
    i2cTransaction.targetAddress = TPS25751_I2C_TARGET_ADDR;

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

        I2C_transfer(i2c, &i2cTransaction);
    }

    /* Clearing any lingering interrupts. */
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

    /* Setting up the I2C Write */
    Display_printf(display, 0, 0, "Setting up I2C write...");

    /* Note we are adding the +1 here to account for the register offset byte
        that gets sent at the start of the transaction */
    curI2CWrite.bits.numOfBytesPayload = sizeof(curI2CBuffer) + 1;
    curI2CWrite.bits.registerOffset = 0xA5;
    curI2CWrite.bits.targetAddr = 0x42;
    memcpy(curI2CWrite.bits.payloadBuffer, curI2CBuffer, sizeof(curI2CBuffer));
    
    /* Setting up the actual I2C transaction to populate the data register*/
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    memcpy(&curWriteCommand.registerData, &curI2CWrite.bytes, sizeof(tI2CwDataReg));
    i2cTransaction.writeBuf   = &curWriteCommand;
    i2cTransaction.readCount = 0;
    i2cTransaction.writeCount = sizeof(tI2CwDataReg) + 1;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Now that data register is populated, issuing 4CC command to write. */
    Display_printf(display, 0, 0, "Issuing I2Cw 4CC command");
    i2cTransaction.writeBuf = (void*)&i2cWriteCommand;
    i2cTransaction.writeCount = sizeof(t4CCCommand);
    i2cTransaction.readCount  = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "Error issuing 4CC command\n");
        goto TPS25751ErrorClosure;
    }

    /* Waiting for an interrupt to signify CMD1 complete */
    Display_printf(display, 0, 0, "Waiting for CMD1 complete interrupt...");
    do
    {
        xSemaphoreTake(xSemaphore, portMAX_DELAY);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    } while (GPIO_read(CONFIG_GPIO_PD_IRQ));

    /* Putting a breakpoint here for debug demonstration */
    __NOP();

    /* Clearing any lingering interrupts. */
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
    
    /* Setting up the I2C Read */
    Display_printf(display, 0, 0, "Setting up I2C read...");
    curI2CRead.bits.numOfBytesPayload = sizeof(curI2CReadBuffer);
    curI2CRead.bits.registerOffset = 0xA5;
    curI2CRead.bits.targetAddr = 0x42;

    /* Setting up the actual I2C transaction to populate the data register */
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    memcpy(&curWriteCommand.registerData, &curI2CRead.bytes, sizeof(tI2CrDataReg));
    i2cTransaction.writeBuf   = &curWriteCommand;
    i2cTransaction.readCount = 0;
    i2cTransaction.writeCount = sizeof(tI2CrDataReg) + 1;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Now that data register is populated, issuing 4CC command to read */
    Display_printf(display, 0, 0, "Issuing I2Cr 4CC command");
    i2cTransaction.writeBuf = (void*)&i2cReadCommand;
    i2cTransaction.writeCount = sizeof(t4CCCommand);
    i2cTransaction.readCount  = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "Error issuing 4CC command\n");
        goto TPS25751ErrorClosure;
    }

    /* Waiting for an interrupt to signify CMD1 complete */
    Display_printf(display, 0, 0, "Waiting for CMD1 complete interrupt...");
    do
    {
        xSemaphoreTake(xSemaphore, portMAX_DELAY);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    } while (GPIO_read(CONFIG_GPIO_PD_IRQ));

    /* Reading the response */
    addrReg = TPS25751_CMD1_DATA_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = i2cReadRespReg.bytes;
    i2cTransaction.readCount  = sizeof(tI2CrRespReg);

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(i2cReadRespReg.bits.status != 0x00)
    {
        Display_printf(display, 0, 0, "Invalid I2Cr response 0x%x", i2cReadRespReg.bits.status);
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "I2Cr Successful. Payload:");
    for(ii=0;ii<TPS25751_I2C_PAYLOAD_SIZE;ii+=5)
    {
        Display_printf(display, 0, 0, "0x%x 0x%x 0x%x 0x%x 0x%x", 
                        i2cReadRespReg.bits.payLoadResp[ii], i2cReadRespReg.bits.payLoadResp[ii+1], 
                        i2cReadRespReg.bits.payLoadResp[ii+2], i2cReadRespReg.bits.payLoadResp[ii+3],
                        i2cReadRespReg.bits.payLoadResp[ii+4]);
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
