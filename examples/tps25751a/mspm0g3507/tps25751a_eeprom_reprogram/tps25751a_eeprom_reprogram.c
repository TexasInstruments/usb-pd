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
#include "tps25751a_eeprom_reprogram.h"

/* Functions/Structures for driver development */
void interruptEventCallback(uint_least8_t index);
static void drainSemaphoreObject(SemaphoreHandle_t xSemaphore);
static bool pendOnCMD1Interrupt(void);
static bool writeEEPROMData(uint8_t* writeData, uint16_t destAddr, size_t dataSize, bool doFLRD);
static bool readEEPROMData(uint16_t readAddr, uint8_t* readData, size_t readSize);

/* Driver/RTOS Objects */
static I2C_Handle i2cHandle;
static Display_Handle display;
SemaphoreHandle_t xSemaphore;

/* Event register to read currently triggered flags */
static tIntEventRegister curTriggeredIntsReg;
static tBootFlagsRegister curBootFlagsReg;
static tIntEventRegister curEventRegister;
static tIntEventRegister clearAllEventReg;

/* Local variables for interacting with TPS25751A */
static tTPS25751WriteCommand curWriteCommand;
static tCustomerUseRegister custReg;
static tFLadDataRegister curFlADData = 
{
    .bits.numOfBytes = TPS25751_FLAD_DATA_PAYLOAD_SIZE-1,
};
static tFLrdDataRegister curFlRDData = 
{
    .bits.numOfBytes = TPS25751_FLRD_DATA_PAYLOAD_SIZE-1,
};
static tFLvyDataRegister curFlVYData = 
{
    .bits.numOfBytes = TPS25751_FLVY_DATA_PAYLOAD_SIZE-1,
}; 
static tFLrdResponse flrdResp;
static tFLwdDataRegister flwdData;
static tFLvyResponse flvyResp;

/* 4CC commands */
const t4CCCommand flad4CCCommand = 
{
    .commandRegister = TPS25751_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25751_4CC_FLad_CMD
};

const t4CCCommand flwd4CCCommand = 
{
    .commandRegister = TPS25751_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25751_4CC_FLwd_CMD
};

const t4CCCommand flvy4CCCommand = 
{
    .commandRegister = TPS25751_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25751_4CC_FLvy_CMD
};

const t4CCCommand flrd4CCCommand = 
{
    .commandRegister = TPS25751_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25751_4CC_FLrd_CMD
};

const t4CCCommand GAID4CCCommand = 
{
    .commandRegister = TPS25751_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25751_4CC_GAID_CMD
};

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    I2C_Params i2cParams;
    I2C_Transaction i2cTransaction;
    uint8_t addrReg;
    tModeRegister modeReg;
    uint32_t newRegPointer, newRegStart;
    uint32_t oldRegPointer, oldRegStart;
    uint32_t ii, curSize;
    uint16_t zeroedPointer = 0;

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

    /* Initializing the initial structures */
    memset(curEventRegister.bytes + 1, 0x00, sizeof(tIntEventRegister) - 1);
    memset(clearAllEventReg.bytes + 1, 0xFF, sizeof(tIntEventRegister) - 1);

    Display_printf(display, 0, 0, "\n--- TPS25751A EEPROM Reprogrammer ---");

    /* Create I2C for usage */
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_100kHz;
    i2cHandle = I2C_open(CONFIG_I2C_TMP, &i2cParams);
    if (i2cHandle == NULL)
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

    /* Waiting for the device to be booted  */
    modeReg.mode = 0;
    addrReg = TPS25751_MODE_REG;
    Display_printf(display, 0, 0, "Waiting for device to boot...");
    while ((modeReg.mode != TPS25751_MODE_APP))
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);

        i2cTransaction.writeBuf   = &addrReg;
        i2cTransaction.writeCount = 1;
        i2cTransaction.readBuf    = &modeReg;
        i2cTransaction.readCount  = sizeof(tModeRegister);

        I2C_transfer(i2cHandle, &i2cTransaction);
    }

    Display_printf(display, 0, 0, "    Device is booted!");
    
    /* Setting interrupt mask to enable CMD1  */
    Display_printf(display, 0, 0, "Enabling CMD1 interrupts...");
    curEventRegister.bits.cmd1Complete = 1;
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_MASK_REG;
    memcpy(&curWriteCommand.registerData, &curEventRegister.bytes, sizeof(tIntEventRegister));
    i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
    i2cTransaction.writeBuf = &curWriteCommand;
    i2cTransaction.readCount = 0;

    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "    Enabled!");

    /* Clearing all active interrupts */
    Display_printf(display, 0, 0, "Clearing lingering interrupts...");
    memcpy(&curWriteCommand.registerData, &clearAllEventReg.bytes, sizeof(tIntEventRegister));
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    i2cTransaction.writeBuf = &curWriteCommand;
    i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
    i2cTransaction.readCount = 0;

    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "    Interrupts cleared!");

    /* Making sure interrupt is not pended */
    drainSemaphoreObject(xSemaphore);

    /* Checking to see if region 0 of the EEPROM has an error or is invalid */

    /* Reading the boot flags register */
    Display_printf(display, 0, 0, "Reading boot flags register...");
    addrReg = TPS25751_BOOT_FLAGS_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &curBootFlagsReg;
    i2cTransaction.readCount  = sizeof(tBootFlagsRegister);

    if(I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "    NAK on boot flags read!");
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "    Read!");

    /* Setting the appropriate pointer variables to write depending on if region 0 is
        valid or not */
    if(curBootFlagsReg.bits.region0EEPROMError || curBootFlagsReg.bits.region0Invalid)
    {
        newRegPointer = 0x0400;
        newRegStart = 0x0800;
        oldRegPointer = 0;
        oldRegStart = 0x4400;
        Display_printf(display, 0, 0, "    EEPROM region set to region 0.");
    }
    else
    {
        newRegPointer = 0;
        newRegStart = 0x4400;
        oldRegPointer = 0x0400;
        oldRegStart = 0x800;
        Display_printf(display, 0, 0, "    EEPROM region set to region 1.");
    }

    Display_printf(display, 0, 0, "Writing region pointer at 0x%x to 0x%x...",newRegPointer,
                                                     zeroedPointer);

    /* Writing the New Region Pointer */
    if(writeEEPROMData((uint8_t*)&zeroedPointer, newRegPointer,
                sizeof(uint16_t), true) == false)
    {
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "    Written!");

    /* Carving new data up into 32 byte chunks and writing them via FLwd */
    Display_printf(display, 0, 0, "Carving up data and sending data...");
    Display_printf(display, 0, 0, "    starting at address 0x%x",newRegStart);
        
    for(ii=0;ii<gSizeLowRegionArray;ii+=TPS25751_EEPROM_SEG_SIZE)
    {
        curSize = (ii+TPS25751_EEPROM_SEG_SIZE) > gSizeLowRegionArray ? 
                (gSizeLowRegionArray - ii) : TPS25751_EEPROM_SEG_SIZE;
        
        if(writeEEPROMData((uint8_t*)(tps25750x_lowRegion_i2c_array + ii), newRegStart + ii, curSize,
                        false) == false)
        {
            goto TPS25751ErrorClosure;
        }
    }

    Display_printf(display, 0, 0, "    All bytes transferred!");

    /* Executing FLvy to verify */
    Display_printf(display, 0, 0, "Sending FLvy command...");

    /* Setting the verify address */
    curFlVYData.bits.verifyAddr = newRegStart;
    memcpy(&curWriteCommand.registerData, &curFlVYData.bytes, sizeof(tFLvyDataRegister));
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    
    /* Writing the DATA1 registers for FLvy */
    i2cTransaction.writeBuf   = &curWriteCommand;
    i2cTransaction.writeCount = sizeof(tFLvyDataRegister) + 1;
    i2cTransaction.readCount  = 0;
    
    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        return (false);
    }

    i2cTransaction.writeBuf = (void*)&flvy4CCCommand;
    i2cTransaction.writeCount = sizeof(t4CCCommand);
    i2cTransaction.readCount  = 0;

    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "    Error issuing FLvy command\n");
        goto TPS25751ErrorClosure;
    }

    if(pendOnCMD1Interrupt() == false)
    {
        Display_printf(display, 0, 0, "    Error waiting for CMD1 interrupt!\n");
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "    Command issued!");

    /* Reading back the data and making sure verify actually passed  */
    addrReg = TPS25751_CMD1_DATA_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &flvyResp;
    i2cTransaction.readCount  = sizeof(tFLvyResponse);
    
    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(flvyResp.bits.returnCode != 0)
    {
        Display_printf(display, 0, 0, "    FLvy failed (0x%x)", flvyResp.bits.returnCode);
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "    FLvy passed!");

    Display_printf(display, 0, 0, "Writing pointer at 0x%x to 0x%x...",newRegPointer, newRegStart);

    /* Writing the New Region Pointer */
    if(writeEEPROMData((uint8_t*)&newRegStart, newRegPointer,
                sizeof(uint16_t), true) == false)
    {
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "    Written!");

    Display_printf(display, 0, 0, "Writing pointer at 0x%x to 0x%x...",oldRegPointer, zeroedPointer);

    /* Writing the Old Region Pointer */
    if(writeEEPROMData((uint8_t*)&zeroedPointer, oldRegPointer,
                sizeof(uint16_t), true) == false)
    {
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "    Written!");

    /* Issuing GAID */
    Display_printf(display, 0, 0, "Sending GAID command...");
    i2cTransaction.writeBuf = (void*)&GAID4CCCommand;
    i2cTransaction.writeCount = sizeof(t4CCCommand);
    i2cTransaction.readCount  = 0;

    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "    Error issuing GAID command\n");
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "    Sent!");

    /* Delaying and waiting for device to boot (and load the EEPROM) */
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    /* Checking customer use register to make sure we have the updated firmware */
    Display_printf(display, 0, 0, "Reading customer user register...");
    addrReg = TPS25751_CUST_USE_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &custReg;
    i2cTransaction.readCount  = sizeof(tCustomerUseRegister);

    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    Display_printf(display, 0, 0, "    Read!");

    if((custReg.custRegWord1 != 0xCAFEBEEF) || (custReg.custRegWord2 != 0xDEADBEEF))
    {
        Display_printf(display, 0, 0, "ERROR! Customer user registers did not match!");
        goto TPS25751ErrorClosure;
    }
    else
    {
        Display_printf(display, 0, 0, "Customer use registers matched!");
        Display_printf(display, 0, 0, "Device flashed successfully!");
    }

TPS25751ErrorClosure:
    I2C_close(i2cHandle);
    Display_printf(display, 0, 0, "I2C closed!");
    return (NULL);
}

void interruptEventCallback(uint_least8_t index)
{
    xSemaphoreGiveFromISR(xSemaphore, NULL);
}

/* Simple convenience function that will write data to the provided address and then optionally
    read back and verify written data  */
static bool writeEEPROMData(uint8_t* writeData, uint16_t destAddr, size_t dataSize, bool doFLRD)
{
    I2C_Transaction i2cTransaction;
    uint8_t ii;

    if((writeData == NULL) || (dataSize > TPS25751_EEPROM_SEG_SIZE))
    {
        return (false);
    }

    /* Setting the start address */
    curFlADData.bits.newPtr = destAddr;
    memcpy(&curWriteCommand.registerData, &curFlADData.bytes, sizeof(tFLadDataRegister));
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    i2cTransaction.targetAddress = TPS25751_I2C_TARGET_ADDR;
    
    /* Writing the DATA1 registers for FLad */
    i2cTransaction.writeBuf   = &curWriteCommand;
    i2cTransaction.writeCount = sizeof(tFLadDataRegister) + 1;
    i2cTransaction.readCount  = 0;
    
    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        return (false);
    }

    /* Writing the actual FLad command */
    i2cTransaction.writeBuf = (void*)&flad4CCCommand;
    i2cTransaction.writeCount = sizeof(t4CCCommand);
    i2cTransaction.readCount  = 0;

    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "    Error issuing FLad command\n");
        return (false);
    }

    if(pendOnCMD1Interrupt() == false)
    {
        Display_printf(display, 0, 0, "    Error waiting for CMD1 interrupt!\n");
        return (false);
    }

    /* Setting up FLwd DATA1 register */
    memcpy(&flwdData.bits.payLoadData, writeData, dataSize);
    flwdData.bits.numOfBytes = dataSize;
    memcpy(&curWriteCommand.registerData, &flwdData.bytes, sizeof(tFLwdDataRegister));
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    i2cTransaction.writeBuf   = &curWriteCommand;
    i2cTransaction.writeCount = dataSize + 2; // Payload + CMD byte + size byte
    i2cTransaction.readCount  = 0;
    
    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "    USB-PD not responding (NAK)");
        return (false);
    }

    /* Executing the FLwd command */
    i2cTransaction.writeBuf = (void*)&flwd4CCCommand;
    i2cTransaction.writeCount = sizeof(t4CCCommand);
    i2cTransaction.readCount  = 0;

    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "    Error issuing FLwd command\n");
        return (false);
    }

    if(pendOnCMD1Interrupt() == false)
    {
        Display_printf(display, 0, 0, "    Error waiting for CMD1 interrupt!\n");
        return (false);
    }

    /* Read back the value if desired */
    if(doFLRD == true)
    {
        if(readEEPROMData(destAddr, writeData, dataSize) == false)
        {
            Display_printf(display, 0, 0, "    Error reading data back!\n");
        }
        
        for(ii=0;ii<dataSize;ii++)
        {
            if(flrdResp.bits.readData[ii] != writeData[ii])
            {
                Display_printf(display, 0, 0, "    Error! Read data didn't match!\n");
                return (false);
            }
        }
    }

    return (true);
}

/* Convenience function that reads back data from EEPROM */
static bool readEEPROMData(uint16_t readAddr, uint8_t* readData, size_t readSize)
{
    I2C_Transaction i2cTransaction;

    /* Setting the start address */
    curFlRDData.bits.readAddr = readAddr;
    memcpy(&curWriteCommand.registerData, &curFlRDData.bytes, sizeof(tFLrdDataRegister));
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    
    /* Writing the DATA1 registers for FLad */
    i2cTransaction.writeBuf   = &curWriteCommand;
    i2cTransaction.writeCount = sizeof(tFLrdDataRegister) + 1;
    i2cTransaction.readCount  = 0;
    
    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        return (false);
    }

    /* Writing the actual FLrd command */
    i2cTransaction.writeBuf = (void*)&flrd4CCCommand;
    i2cTransaction.writeCount = sizeof(t4CCCommand);
    i2cTransaction.readCount  = 0;

    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "    Error issuing FLad command\n");
        return (false);
    }

    if(pendOnCMD1Interrupt() == false)
    {
        Display_printf(display, 0, 0, "    Error waiting for CMD1 interrupt!\n");
        return (false);
    }

    /* Reading back the data and verifying */
    readAddr = TPS25751_CMD1_DATA_REG;
    i2cTransaction.writeBuf   = &readAddr;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &flrdResp;
    i2cTransaction.readCount  = sizeof(tFLrdResponse);
    
    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "USB-PD not responding (NAK)");
        return (false);
    }

    return (true);
}

/* This function will pend on xSemaphore which in turn pends on the IRQ line of I2Ct
    going low. Specifically, this function will wait indefinitely until the CMD1 Complete
    interrupt triggers */
static bool pendOnCMD1Interrupt(void)
{
    uint8_t addrReg;
    I2C_Transaction i2cTransaction;

PendOnCMD1Int:
   /* Pending on CMD1 Completion interrupt*/
    xSemaphoreTake(xSemaphore, portMAX_DELAY);

    /* Reading the currently triggered interrupts and clearing them */
    addrReg = TPS25751_INT_EVENT_REG;
    i2cTransaction.writeBuf = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.targetAddress = TPS25751_I2C_TARGET_ADDR;
    i2cTransaction.readCount = sizeof(tIntEventRegister);
    i2cTransaction.readBuf = &curTriggeredIntsReg;

   if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
   {
       Display_printf(display, 0, 0, "    USB-PD not responding (NAK)");
       return (false);
   }

    /* Clear lingering interrupt*/
    memcpy(&curWriteCommand.registerData, &curTriggeredIntsReg.bytes, sizeof(tIntEventRegister));
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    i2cTransaction.writeBuf = &curWriteCommand;
    i2cTransaction.writeCount = sizeof(tIntEventRegister) + 1;
    i2cTransaction.readCount = 0;

    if (I2C_transfer(i2cHandle, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "    USB-PD not responding (NAK)");
       return (false);
    }

    /* If it is not the CMD1 trigger interrupt, go back and try again */
    if(curTriggeredIntsReg.bits.cmd1Complete != 0x01)
    {
         goto PendOnCMD1Int;
    }

    return (true);

}

static void drainSemaphoreObject(SemaphoreHandle_t xSemaphore)
{
    while(xSemaphoreTake(xSemaphore, 0) == pdTRUE)
    {
        // Keep taking until no more tokens are available
    }
}

