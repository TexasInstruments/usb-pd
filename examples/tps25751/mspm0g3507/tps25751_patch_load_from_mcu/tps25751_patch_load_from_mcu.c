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

/* USB Configuration */
#include "tps25751.h"
#include "tps25751_patch_load_from_mcu.h"
#include "tps_usbpd_i2c_driver.h"

/* USB Structures */

/* This register is used as a local copy and to manage
    the INT_EVENT register from the TPS25751 */
static tIntEventRegister curEventRegister;

/* This data structure is used to clear all interrupt flags
    and is initialized to all 0xFFs */
static tIntEventRegister clearAllEventReg;

/* Event register to read currently triggered flags */
static tIntEventRegister curTriggeredIntsReg;

/* This command is used as inputs to the PBMs 4CC command */
static tPBMDataReg curPBMDataReg = 
{
    .bits.numOfBytes = TPS25751_PBM_DATA_PAYLOAD_SIZE-1,
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

static t4CCCommandResp cmdResp;

/* Customer Use Registers */
static tCustomerUseRegister custReg;

/* Local variable for I2C driver communication */
static tTPS25751WriteCommand curWriteCommand;
static tPBMsResponse pbmsResp;
static tPBMcResponse pbmcResp;

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    uint8_t addrReg;
    uint8_t sizeWritten;
    uint32_t ii;
    tModeRegister modeReg;

    /* Call driver init functions and create RTOS objects */
    TPS_USBPD_initializeDevice();

    /* Initializing the initial structures */
    memset(curEventRegister.bytes + 1, 0x00, sizeof(tIntEventRegister) - 1);
    curEventRegister.bits.cmd1Complete = 1;
    curEventRegister.bits.rdyForPatch = 1;
    memset(clearAllEventReg.bytes + 1, 0xFF, sizeof(tIntEventRegister) - 1);

    TPS_USBPD_logMessage("\n--- Patch Load from MCU ---");

    /* Waiting for the device to be in PTCH mode  */
    modeReg.mode = 0;
    addrReg = TPS25751_MODE_REG;
    TPS_USBPD_logMessage("Waiting for device to be in PTCH mode...");
    while (modeReg.mode != TPS25751_MODE_PTCH)
    {
        TPS_USBPD_delayMS(10);
        TPS_USBPD_i2cTransfer(&addrReg, 1, &modeReg, sizeof(tModeRegister));
    }

    /* Setting interrupt mask to enable CMD1 complete */
    TPS_USBPD_logMessage("Enabling CMD1 interrupts...");
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_MASK_REG;
    memcpy(&curWriteCommand.registerData, &curEventRegister.bytes, sizeof(tIntEventRegister));
   
    if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Clearing all active interrupts */
    TPS_USBPD_logMessage("Clearing lingering interrupts...");
    memcpy(&curWriteCommand.registerData, &clearAllEventReg.bytes, sizeof(tIntEventRegister));
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Send PBMs Data */
    TPS_USBPD_logMessage("Setting PBMs data...");
    curPBMDataReg.bits.lowerRegionSize = gSizeLowRegionArray;
    memcpy(&curWriteCommand.registerData, &curPBMDataReg.bytes, sizeof(tPBMDataReg));
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    
    if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tPBMDataReg) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Reading back the PBMs data to verify correct write */
    addrReg = TPS25751_CMD1_DATA_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &readPBMDataReg, sizeof(tPBMDataReg)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    for(ii=1; ii < sizeof(tPBMDataReg); ii++)
    {
        if(readPBMDataReg.bytes[ii] != curPBMDataReg.bytes[ii])
        {
            TPS_USBPD_logMessage("PBMs data register failed to write!");
            goto TPS25751ErrorClosure;
        }

    }

    /* Sending PMBs Command */
    TPS_USBPD_logMessage("Sending PBMs command...");
    if(TPS_USBPD_i2cTransfer((void*)&pbms4CCCommand, sizeof(t4CCCommand), NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Waiting for CMD1 interrupt */
WaitForPMBSCMD:
    TPS_USBPD_pendOnIRQ(UINT32_MAX);

    /* Reading the currently triggered interrupts and clearing them */
    addrReg = TPS25751_INT_EVENT_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &curTriggeredIntsReg, sizeof(tIntEventRegister)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Clear lingering interrupt*/
    memcpy(&curWriteCommand.registerData, &clearAllEventReg.bytes, sizeof(tIntEventRegister));
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    if(TPS_USBPD_i2cTransfer((void*)&curWriteCommand, sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(curTriggeredIntsReg.bits.cmd1Complete != 0x01)
    {
         TPS_USBPD_logMessage("Interrupt happened, but it wasn't CMD1.");
         goto WaitForPMBSCMD;
    }

    TPS_USBPD_logMessage("CMD1 interrupt occured!");

    /* Reading the CMD1 register to make sure the command went through */
    addrReg = TPS25751_4CC_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &cmdResp, sizeof(t4CCCommandResp)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(cmdResp.bits.commandStatus != 0)
    {
        TPS_USBPD_logMessage("PBMs command rejected!");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("PBMs command accepted!");

    /* Reading output data to verify command success */
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &pbmsResp,sizeof(tPBMsResponse)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(pbmsResp.bits.status != 0x00)
    {
        TPS_USBPD_logMessage("Invalid PBMs response 0x%x", pbmsResp.bits.status);
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("CMD1 DATA read and task successful.");
    TPS_USBPD_logMessage("Sending data...");

    /* We have to set the target address to the burst register */
    TPS_USBPD_setI2CTargetAddress(TPS25751_BURST_REG);


    if(TPS_USBPD_i2cTransfer((uint8_t*)&tps25751x_lowRegion_i2c_array, gSizeLowRegionArray, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("Data transfer done!");
    TPS_USBPD_logMessage("Size written: %d", gSizeLowRegionArray);


    /* Setting interrupt mask to enable CMD1 complete and PATCH loaded */
    TPS_USBPD_logMessage("Enabling CMD1 interrupts...");
    TPS_USBPD_setI2CTargetAddress(TPS25751_I2C_TARGET_ADDR);
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_MASK_REG;
    memcpy(&curWriteCommand.registerData, &curEventRegister.bytes, sizeof(tIntEventRegister));
    if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Send PBMc command to device */
    TPS_USBPD_logMessage("Sending PBMc command...");
    if(TPS_USBPD_i2cTransfer((void*)&pbmc4CCCommand, sizeof(t4CCCommand), NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

   /* Waiting for CMD1 interrupt */
WaitForPMBCCMD:
    TPS_USBPD_pendOnIRQ(UINT32_MAX);

    /* Reading the currently triggered interrupts and clearing them */
    addrReg = TPS25751_INT_EVENT_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &curTriggeredIntsReg, sizeof(tIntEventRegister)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Clear lingering interrupt*/
    memcpy(&curWriteCommand.registerData, &curTriggeredIntsReg.bytes, sizeof(tIntEventRegister));
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(curTriggeredIntsReg.bits.cmd1Complete != 0x01)
    {
         TPS_USBPD_logMessage("Interrupt happened, but it wasn't CMD1.");
         goto WaitForPMBCCMD;
    }

    TPS_USBPD_logMessage("CMD1 interrupt occured!");

    /* Reading the CMD1 register to make sure the command wasn't rejected */
    TPS_USBPD_delayMS(5);

    addrReg = TPS25751_4CC_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &cmdResp, sizeof(t4CCCommandResp)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(cmdResp.bits.commandStatus != 0)
    {
        TPS_USBPD_logMessage("PBMc CMD1 not clear!");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("PBMc CMD1 accepted!");

    /* Reading the result register from data */
    addrReg = TPS25751_CMD1_DATA_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &pbmcResp, sizeof(tPBMcResponse)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(pbmcResp.bits.status.standardTaskResult != 0)
    {
        TPS_USBPD_logMessage("PBMc command failed to send: 0x%x", pbmcResp.bits.status.standardTaskResult);
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_delayMS(20);

    /* Reading the MODE register to verify we are now in APP  mode */
    TPS_USBPD_logMessage("Waiting for device to be in APP mode...");
    addrReg = TPS25751_MODE_REG;
    modeReg.mode = 0;  
    while (modeReg.mode != TPS25751_MODE_APP)
    {
        TPS_USBPD_delayMS(10);
        TPS_USBPD_i2cTransfer(&addrReg, 1, &modeReg, sizeof(tModeRegister));
    }

    TPS_USBPD_logMessage("Device is in APP, patch loaded!");

    /* Reading customer use register 1 */
    TPS_USBPD_logMessage("Reading customer user register...");
    addrReg = TPS25751_CUST_USE_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &custReg, sizeof(tCustomerUseRegister)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if((custReg.custRegWord2 != 0xCAFEBEEF) || (custReg.custRegWord1 != 0xDEADBEEF))
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
