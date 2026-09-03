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

/* Driver configuration */
#include "ti_drivers_config.h"

/* USB Configuration */
#include "tps25751.h"
#include "tps25751a_eeprom_reprogram.h"
#include "tps_usbpd_i2c_driver.h"

/* Functions/Structures for driver development */
static bool pendOnCMD1Interrupt(void);
static bool writeEEPROMData(uint8_t* writeData, uint16_t destAddr, size_t dataSize, bool doFLRD);
static bool readEEPROMData(uint16_t readAddr, uint8_t* readData, size_t readSize);

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
    uint8_t addrReg;
    tModeRegister modeReg;
    uint32_t newRegPointer, newRegStart;
    uint32_t oldRegPointer, oldRegStart;
    uint32_t ii, curSize;
    uint16_t zeroedPointer = 0;

    /* Call driver init functions and create RTOS objects */
    TPS_USBPD_initializeDevice();

    /* Initializing the initial structures */
    memset(curEventRegister.bytes + 1, 0x00, sizeof(tIntEventRegister) - 1);
    memset(clearAllEventReg.bytes + 1, 0xFF, sizeof(tIntEventRegister) - 1);

    TPS_USBPD_logMessage("\n--- TPS25751A EEPROM Reprogrammer ---");

    /* Waiting for the device to be booted  */
    modeReg.mode = 0;
    addrReg = TPS25751_MODE_REG;
    TPS_USBPD_logMessage("Waiting for device to boot...");
    while ((modeReg.mode != TPS25751_MODE_APP))
    {
        TPS_USBPD_delayMS(10);
        TPS_USBPD_i2cTransfer(&addrReg, 1, &modeReg, sizeof(tModeRegister));
    }

    TPS_USBPD_logMessage("    Device is booted!");
    
    /* Setting interrupt mask to enable CMD1  */
    TPS_USBPD_logMessage("Enabling CMD1 interrupts...");
    curEventRegister.bits.cmd1Complete = 1;
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_MASK_REG;
    memcpy(&curWriteCommand.registerData, &curEventRegister.bytes, sizeof(tIntEventRegister));

    if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("    Enabled!");

    /* Clearing all active interrupts */
    TPS_USBPD_logMessage("Clearing lingering interrupts...");
    memcpy(&curWriteCommand.registerData, &clearAllEventReg.bytes, sizeof(tIntEventRegister));
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("    Interrupts cleared!");

    /* Checking to see if region 0 of the EEPROM has an error or is invalid */

    /* Reading the boot flags register */
    TPS_USBPD_logMessage("Reading boot flags register...");
    addrReg = TPS25751_BOOT_FLAGS_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &curBootFlagsReg, sizeof(tBootFlagsRegister)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("    Read!");

    /* Setting the appropriate pointer variables to write depending on if region 0 is
        valid or not */
    if(curBootFlagsReg.bits.region0EEPROMError || curBootFlagsReg.bits.region0Invalid)
    {
        newRegPointer = 0x0400;
        newRegStart = 0x0800;
        oldRegPointer = 0;
        oldRegStart = 0x4400;
        TPS_USBPD_logMessage("    EEPROM region set to region 0.");
    }
    else
    {
        newRegPointer = 0;
        newRegStart = 0x4400;
        oldRegPointer = 0x0400;
        oldRegStart = 0x800;
        TPS_USBPD_logMessage("    EEPROM region set to region 1.");
    }

    TPS_USBPD_logMessage("Writing region pointer at 0x%x to 0x%x...", newRegPointer,
                                                     zeroedPointer);

    /* Writing the New Region Pointer */
    if(writeEEPROMData((uint8_t*)&zeroedPointer, newRegPointer,
                sizeof(uint16_t), true) == false)
    {
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("    Written!");

    /* Carving new data up into 32 byte chunks and writing them via FLwd */
    TPS_USBPD_logMessage("Carving up data and sending data...");
    TPS_USBPD_logMessage("    starting at address 0x%x",newRegStart);
        
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

    TPS_USBPD_logMessage("    All bytes transferred!");

    /* Executing FLvy to verify */
    TPS_USBPD_logMessage("Sending FLvy command...");

    /* Setting the verify address */
    curFlVYData.bits.verifyAddr = newRegStart;
    memcpy(&curWriteCommand.registerData, &curFlVYData.bytes, sizeof(tFLvyDataRegister));
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    
    /* Writing the DATA1 registers for FLvy and issuing command */
    if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tFLvyDataRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("    Error issuing FLvy command\n");
        goto TPS25751ErrorClosure;
    }

    if(TPS_USBPD_i2cTransfer((void*)&flvy4CCCommand, sizeof(t4CCCommand), NULL, 0) == false)
    {
        TPS_USBPD_logMessage("    Error issuing FLvy command\n");
        goto TPS25751ErrorClosure;
    }

    if(pendOnCMD1Interrupt() == false)
    {
        TPS_USBPD_logMessage("    Error waiting for CMD1 interrupt!\n");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("    Command issued!");

    /* Reading back the data and making sure verify actually passed  */
    addrReg = TPS25751_CMD1_DATA_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &flvyResp, sizeof(tFLvyResponse)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(flvyResp.bits.returnCode != 0)
    {
        TPS_USBPD_logMessage("    FLvy failed (0x%x)", flvyResp.bits.returnCode);
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("    FLvy passed!");

    TPS_USBPD_logMessage("Writing pointer at 0x%x to 0x%x...",newRegPointer, newRegStart);

    /* Writing the New Region Pointer */
    if(writeEEPROMData((uint8_t*)&newRegStart, newRegPointer,
                sizeof(uint16_t), true) == false)
    {
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("    Written!");

    TPS_USBPD_logMessage("Writing pointer at 0x%x to 0x%x...",oldRegPointer, zeroedPointer);

    /* Writing the Old Region Pointer */
    if(writeEEPROMData((uint8_t*)&zeroedPointer, oldRegPointer,
                sizeof(uint16_t), true) == false)
    {
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("    Written!");

    /* Issuing GAID */
    TPS_USBPD_logMessage("Sending GAID command...");
    if(TPS_USBPD_i2cTransfer((void*)&GAID4CCCommand, sizeof(t4CCCommand), NULL, 0) == false)
    {
        TPS_USBPD_logMessage("    Error issuing GAID command\n");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("    Sent!");

    /* Delaying and waiting for device to boot (and load the EEPROM) */
    TPS_USBPD_delayMS(5000);

    /* Checking customer use register to make sure we have the updated firmware */
    TPS_USBPD_logMessage("Reading customer user register...");
    addrReg = TPS25751_CUST_USE_REG;
    if(TPS_USBPD_i2cTransfer((void*)&addrReg, 1, &custReg, sizeof(tCustomerUseRegister)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("    Read!");

    if((custReg.custRegWord1 != 0xCAFEBEEF) || (custReg.custRegWord2 != 0xDEADBEEF))
    {
        TPS_USBPD_logMessage("ERROR! Customer user registers did not match!");
        goto TPS25751ErrorClosure;
    }
    else
    {
        TPS_USBPD_logMessage("Customer use registers matched!");
        TPS_USBPD_logMessage("Device flashed successfully!");
    }

TPS25751ErrorClosure:
    TPS_USBPD_closeDevice();
    return (NULL);
}


/* Simple convenience function that will write data to the provided address and then optionally
    read back and verify written data  */
static bool writeEEPROMData(uint8_t* writeData, uint16_t destAddr, size_t dataSize, bool doFLRD)
{
    uint8_t ii;

    if((writeData == NULL) || (dataSize > TPS25751_EEPROM_SEG_SIZE))
    {
        return (false);
    }

    /* Setting the start address */
    curFlADData.bits.newPtr = destAddr;
    memcpy(&curWriteCommand.registerData, &curFlADData.bytes, sizeof(tFLadDataRegister));
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    
    /* Writing the DATA1 registers for FLad */
    if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tFLadDataRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        return (false);
    }

    /* Writing the actual FLad command */
    if(TPS_USBPD_i2cTransfer((void*)&flad4CCCommand, sizeof(t4CCCommand), NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        return (false);
    }

    if(pendOnCMD1Interrupt() == false)
    {
        TPS_USBPD_logMessage("    Error waiting for CMD1 interrupt!\n");
        return (false);
    }

    /* Setting up FLwd DATA1 register */
    memcpy(&flwdData.bits.payLoadData, writeData, dataSize);
    flwdData.bits.numOfBytes = dataSize;
    memcpy(&curWriteCommand.registerData, &flwdData.bytes, sizeof(tFLwdDataRegister));
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;

    if(TPS_USBPD_i2cTransfer(&curWriteCommand, dataSize + 2, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        return (false);
    }

    if(TPS_USBPD_i2cTransfer((void*)&flwd4CCCommand, sizeof(t4CCCommand), NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        return (false);
    }

    if(pendOnCMD1Interrupt() == false)
    {
        TPS_USBPD_logMessage("    Error waiting for CMD1 interrupt!\n");
        return (false);
    }

    /* Read back the value if desired */
    if(doFLRD == true)
    {
        if(readEEPROMData(destAddr, writeData, dataSize) == false)
        {
            TPS_USBPD_logMessage("    Error reading data back!\n");
        }
        
        for(ii=0;ii<dataSize;ii++)
        {
            if(flrdResp.bits.readData[ii] != writeData[ii])
            {
                TPS_USBPD_logMessage("    Error! Read data didn't match!\n");
                return (false);
            }
        }
    }

    return (true);
}

/* Convenience function that reads back data from EEPROM */
static bool readEEPROMData(uint16_t readAddr, uint8_t* readData, size_t readSize)
{
    /* Setting the start address */
    curFlRDData.bits.readAddr = readAddr;
    memcpy(&curWriteCommand.registerData, &curFlRDData.bytes, sizeof(tFLrdDataRegister));
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    
    /* Writing the DATA1 registers for FLad */
    if(TPS_USBPD_i2cTransfer((void*)&curWriteCommand, sizeof(tFLrdDataRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        return (false);
    }

    /* Writing the actual FLrd command */
    if(TPS_USBPD_i2cTransfer((void*)&flrd4CCCommand, sizeof(t4CCCommand), NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        return (false);
    }

    if(pendOnCMD1Interrupt() == false)
    {
        TPS_USBPD_logMessage("    Error waiting for CMD1 interrupt!\n");
        return (false);
    }

    /* Reading back the data and verifying */
    readAddr = TPS25751_CMD1_DATA_REG;
    if(TPS_USBPD_i2cTransfer((void*)&readAddr, 1, &flrdResp, sizeof(tFLrdResponse)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
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
    TPS_USBPD_pendOnIRQ(UINT32_MAX);

    /* Reading the currently triggered interrupts and clearing them */
    addrReg = TPS25751_INT_EVENT_REG;
    if(TPS_USBPD_i2cTransfer((void*)&addrReg, 1, &curTriggeredIntsReg, sizeof(tIntEventRegister)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        return (false);
    }

    /* Clear lingering interrupt*/
    memcpy(&curWriteCommand.registerData, &curTriggeredIntsReg.bytes, sizeof(tIntEventRegister));
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    if(TPS_USBPD_i2cTransfer((void*)&curWriteCommand, sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        return (false);
    }

    /* If it is not the CMD1 trigger interrupt, go back and try again */
    if(curTriggeredIntsReg.bits.cmd1Complete != 0x01)
    {
         goto PendOnCMD1Int;
    }

    return (true);

}
