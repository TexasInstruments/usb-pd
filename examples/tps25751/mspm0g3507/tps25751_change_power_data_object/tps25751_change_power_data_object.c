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
tModeRegister modeRegister;
tActiveRDORegister curRDORegister;
tIntEventRegister curEventRegister;
tIntEventRegister tmpEventRegister;
tAutonegotiateSinkRegister curAutoNegRegister;
tTPS25751WriteCommand curWriteCommand;

const t4CCCommand autoNegotiateCommand = 
{
    .commandRegister = TPS25751_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25751_4CC_ANeg_CMD // ASCII ANeg
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

    Display_printf(display, 0, 0, "\n--- TPS25751 Change Power Data Object Example ---");

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

SYSTEM_POWER_ON:
    /* Waiting for USB-PD Source to be plugged in...  */
    Display_printf(display, 0, 0, "Plug in power to TPS25751 to continue...");
    vTaskDelay(500 / portTICK_PERIOD_MS);

    /* Setting up the read transaction to the event register */
    addrReg = TPS25751_MODE_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &modeRegister.numOfBytes;
    i2cTransaction.readCount  = sizeof(tModeRegister);

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto SYSTEM_POWER_ON;
    }

WAIT_FOR_USBPD_CONTRACT:
    /* Waiting for USB-PD Source to be plugged in...  */
    Display_printf(display, 0, 0, "Device powered! Waiting for USB-PD");
    Display_printf(display, 0, 0, "  Charger negotiation...");
    // Only wait for IRQ if not already asserted. If IRQ is asserted, keep executing
    if (GPIO_read(CONFIG_GPIO_PD_IRQ))
    {
        do
        {
            xSemaphoreTake(xSemaphore, portMAX_DELAY);
            vTaskDelay(50 / portTICK_PERIOD_MS);
        } while (GPIO_read(CONFIG_GPIO_PD_IRQ));
    }

    /* Setting up the read transaction to the event register */
    addrReg = TPS25751_INT_EVENT_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &curEventRegister.bytes;
    i2cTransaction.readCount  = sizeof(tIntEventRegister);

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Seeing if system loaded from EEPROM */
    if ( curEventRegister.bits.patchLoaded == 1)
    {
        Display_printf(display, 0, 0, "\nTPS25751 loaded configuration! Clearing flag.");

        /* If system powers on recently. Clear the app loaded flag only */
        curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
        memset(tmpEventRegister.bytes + 1, 0x00, sizeof(tIntEventRegister) - 1);
        tmpEventRegister.bits.patchLoaded = 1;
        memcpy(&curWriteCommand.registerData, &tmpEventRegister, sizeof(tIntEventRegister));
        i2cTransaction.writeBuf = &curWriteCommand;
        i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
        i2cTransaction.readCount = 0;
        
        if (I2C_transfer(i2c, &i2cTransaction) == false)
        {
            Display_printf(display, 0, 0, "Error clearing interrupt event registers!");
            goto TPS25751ErrorClosure;
        }
        curEventRegister.bits.patchLoaded = 0;
    }

    /* Seeing if there was a plug event  */
    if(curEventRegister.bits.plugInsertRemoval == 1)
    {
        Display_printf(display, 0, 0, "\nPlug event detected! Clearing flag.");

        /* If there is a plug event, clear the plug event flag */
        curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
        memset(tmpEventRegister.bytes + 1, 0x00, sizeof(tIntEventRegister) - 1);
        tmpEventRegister.bits.plugInsertRemoval = 1;
        memcpy(&curWriteCommand.registerData, &tmpEventRegister, sizeof(tIntEventRegister));
        i2cTransaction.writeBuf = &curWriteCommand;
        i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
        i2cTransaction.readCount = 0;
        
        if (I2C_transfer(i2c, &i2cTransaction) == false)
        {
            Display_printf(display, 0, 0, "Error clearing interrupt event registers!");
            goto TPS25751ErrorClosure;
        }
        curEventRegister.bits.plugInsertRemoval = 0;
    }

    /* Whenever a new contract as consumer happens, verify it is greater than 5V */
    if (curEventRegister.bits.newContractCons == 1)
    {
        Display_printf(display, 0, 0, "\nNew PDO Contract detected!");
        Display_printf(display, 0, 0, "  Reading information then clearing flag.");
        /* Update the current RDO Contract shadow register */
        addrReg = TPS25751_ACTIVE_RDO_REG;
        i2cTransaction.writeBuf   = &addrReg;
        i2cTransaction.writeCount = 1;
        i2cTransaction.readBuf    = &curRDORegister.bytes;
        i2cTransaction.readCount  = sizeof(tActiveRDORegister);

        if (I2C_transfer(i2c, &i2cTransaction) == false)
        {
            Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
            goto TPS25751ErrorClosure;
        }
        /* If there is a new contract event, clear the plug event flag */
        curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
        memset(tmpEventRegister.bytes + 1, 0x00, sizeof(tIntEventRegister) - 1);
        tmpEventRegister.bits.newContractCons = 1;
        memcpy(&curWriteCommand.registerData, &tmpEventRegister, sizeof(tIntEventRegister));
        i2cTransaction.writeBuf = &curWriteCommand;
        i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
        i2cTransaction.readCount = 0;
        
        if (I2C_transfer(i2c, &i2cTransaction) == false)
        {
            Display_printf(display, 0, 0, "Error clearing interrupt event registers!");
            goto TPS25751ErrorClosure;
        }
        curEventRegister.bits.newContractCons = 0;
        /* Check if requested power object is NOT the first one i.e. (>5V)*/
        if (curRDORegister.bits.objectPosition > 1)
        {
            Display_printf(display, 0, 0, "USB-PD Contract > 5V negotiated!");
            goto POST_INITIAL_USBPD_CONTRACT;
        }
        /* If RDO is the 5V PDO, then nothing to do*/
        if (curRDORegister.bits.objectPosition == 1)
        {
            Display_printf(display, 0, 0, "USB-PD 5V Contract already negotiated!");
            Display_printf(display, 0, 0, "  Nothing left to do...");
            goto TPS25751ErrorClosure;
        }
    }

    /* Clear any remaining interrupts to reset IRQ line */
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    memcpy(&curWriteCommand.registerData, &curEventRegister, sizeof(tIntEventRegister));
    i2cTransaction.writeBuf = &curWriteCommand;
    i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
    i2cTransaction.readCount = 0;
    
    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "Error clearing interrupt event registers!");
        goto TPS25751ErrorClosure;
    }

    /* Go back to top of all events cleared and no new Contract as Consumer event set */
    Display_printf(display, 0, 0, "\nInterrupt Event New Contract as Consumer not");
    Display_printf(display, 0, 0, "  set... waiting for next interrupt.");
    goto WAIT_FOR_USBPD_CONTRACT;

POST_INITIAL_USBPD_CONTRACT:

    /* Clear any remaining interrupts to reset IRQ line */
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    memcpy(&curWriteCommand.registerData, &curEventRegister, sizeof(tIntEventRegister));
    i2cTransaction.writeBuf = &curWriteCommand;
    i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
    i2cTransaction.readCount = 0;
    
    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "Error clearing interrupt event registers!");
        goto TPS25751ErrorClosure;
    }
    
    Display_printf(display, 0, 0, "Reading current autonegotiate sink");
    Display_printf(display, 0, 0, "  register contents...");
    /* Setting up the read transaction to the event register */
    addrReg = TPS25751_AUTONEG_SINK_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &curAutoNegRegister.bytes;
    i2cTransaction.readCount  = sizeof(tAutonegotiateSinkRegister);

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "Changing ");
    Display_printf(display, 0, 0, "  AutonegotiateSink.autoComputeSinkMaxVoltage");
    Display_printf(display, 0, 0, "  to EC-controlled (0x0)");
    curAutoNegRegister.bits.autoComputeSinkMaxVoltage = 0;
    Display_printf(display, 0, 0, "Changing AutonegotiateSink.autoNegMaxVoltage");
    Display_printf(display, 0, 0, "  to 5V (100d)");
    curAutoNegRegister.bits.autoNegMaxVoltage = 100;

    curWriteCommand.writeAddr = TPS25751_AUTONEG_SINK_REG;
    memcpy(&curWriteCommand.registerData, &curAutoNegRegister.bytes, sizeof(tAutonegotiateSinkRegister));
    
    i2cTransaction.writeCount = sizeof(tAutonegotiateSinkRegister) + 1;
    i2cTransaction.writeBuf = &curWriteCommand;
    i2cTransaction.readCount = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }
    else
    {
        Display_printf(display, 0, 0, "Autonegotiate Sink register setup to request");
        Display_printf(display, 0, 0, "  5V Fixed PDO.");
    }

    Display_printf(display, 0, 0, "Issuing ANeg 4CC command");
    i2cTransaction.writeBuf = (void*)&autoNegotiateCommand;
    i2cTransaction.writeCount = sizeof(t4CCCommand);
    i2cTransaction.readCount  = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "Error issuing 4CC command\n");
        goto TPS25751ErrorClosure;
    }
    else
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        goto WAIT_FOR_USBPD_CONTRACT;
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
