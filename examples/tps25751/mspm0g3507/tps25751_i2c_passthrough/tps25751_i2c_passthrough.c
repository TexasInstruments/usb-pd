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
#include "tps25751_i2c_passthrough.h"
#include "tps_usbpd_i2c_driver.h"

/* Buffers used for I2C transaction */
static uint8_t curI2CBuffer[TPS25751_I2C_PAYLOAD_SIZE] = 
{
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBE, 0xEF, 0xBA, 0xBE
};

/* Stores the read response */
static uint8_t curI2CReadBuffer[TPS25751_I2C_PAYLOAD_SIZE];

/* Pends on CMD1 interrupt */
static bool pendOnCMD1Interrupt(void);

/* This data structure is used to clear all interrupt flags
    and is initialized to all 0xFFs */
static tIntEventRegister clearAllEventReg;
static tIntEventRegister curTriggeredIntsReg;

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
    uint8_t addrReg;
    uint32_t ii;

    /* Call driver init functions and create RTOS objects */
    TPS_USBPD_initializeDevice();

    /* Initializing the initial structures. We are assuming only the CMD1 complete
        interrupt is enabled as this is what we configured in the device example
        configuration  */
    memset(curI2CWrite.bytes + 1, 0x00, sizeof(tI2CwDataReg) - 1);
    memset(clearAllEventReg.bytes + 1, 0xFF, sizeof(tIntEventRegister) - 1);

    TPS_USBPD_logMessage("\n--- I2C Passthrough Example ---");

    /* Reading the MODE register to verify we are now in APP  mode */
    TPS_USBPD_logMessage("Waiting for device to be in APP mode...");
    addrReg = TPS25751_MODE_REG;
    modeReg.mode = 0;  
    while (modeReg.mode != TPS25751_MODE_APP)
    {
        TPS_USBPD_delayMS(10);
        TPS_USBPD_i2cTransfer(&addrReg, 1, &modeReg, sizeof(tModeRegister));
    }

    /* Clearing any lingering interrupts. */
    TPS_USBPD_logMessage("Clearing lingering interrupts...");
    memcpy(&curWriteCommand.registerData, &clearAllEventReg.bytes, sizeof(tIntEventRegister));
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    if(TPS_USBPD_i2cTransfer((void*)&curWriteCommand, sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Setting up the I2C Write */
    TPS_USBPD_logMessage("Setting up I2C write...");

    /* Note we are adding the +1 here to account for the register offset byte
        that gets sent at the start of the transaction */
    curI2CWrite.bits.numOfBytesPayload = sizeof(curI2CBuffer) + 1;
    curI2CWrite.bits.registerOffset = 0xA5;
    curI2CWrite.bits.targetAddr = 0x42;
    memcpy(curI2CWrite.bits.payloadBuffer, curI2CBuffer, sizeof(curI2CBuffer));
    
    /* Setting up the actual I2C transaction to populate the data register*/
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    memcpy(&curWriteCommand.registerData, &curI2CWrite.bytes, sizeof(tI2CwDataReg));
    if(TPS_USBPD_i2cTransfer((void*)&curWriteCommand, sizeof(tI2CwDataReg) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Now that data register is populated, issuing 4CC command to write. */
    TPS_USBPD_logMessage("Issuing I2Cw 4CC command...");
    if(TPS_USBPD_i2cTransfer((void*)&i2cWriteCommand, sizeof(t4CCCommand), NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(pendOnCMD1Interrupt() == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }
    
    /* Setting up the I2C Read */
    TPS_USBPD_logMessage("Setting up I2C read...");
    curI2CRead.bits.numOfBytesPayload = sizeof(curI2CReadBuffer);
    curI2CRead.bits.registerOffset = 0xA5;
    curI2CRead.bits.targetAddr = 0x42;

    /* Setting up the actual I2C transaction to populate the data register */
    curWriteCommand.writeAddr = TPS25751_CMD1_DATA_REG;
    memcpy(&curWriteCommand.registerData, &curI2CRead.bytes, sizeof(tI2CrDataReg));
    if(TPS_USBPD_i2cTransfer((void*)&curWriteCommand, sizeof(tI2CrDataReg) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Now that data register is populated, issuing 4CC command to read */
    TPS_USBPD_logMessage("Issuing I2Cr 4CC command...");
    if(TPS_USBPD_i2cTransfer((void*)&i2cReadCommand, sizeof(t4CCCommand), NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Waiting for an interrupt to signify CMD1 complete */
    TPS_USBPD_logMessage("Waiting for CMD1 complete interrupt...");
     if(pendOnCMD1Interrupt() == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Reading the response */
    addrReg = TPS25751_CMD1_DATA_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, i2cReadRespReg.bytes, sizeof(tI2CrRespReg)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    if(i2cReadRespReg.bits.status != 0x00)
    {
        TPS_USBPD_logMessage("Invalid I2Cr response 0x%x", i2cReadRespReg.bits.status);
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage("I2Cr Successful. Payload:");
    for(ii=0;ii<TPS25751_I2C_PAYLOAD_SIZE;ii+=5)
    {
        TPS_USBPD_logMessage("0x%x 0x%x 0x%x 0x%x 0x%x", 
                        i2cReadRespReg.bits.payLoadResp[ii], i2cReadRespReg.bits.payLoadResp[ii+1], 
                        i2cReadRespReg.bits.payLoadResp[ii+2], i2cReadRespReg.bits.payLoadResp[ii+3],
                        i2cReadRespReg.bits.payLoadResp[ii+4]);
    }

TPS25751ErrorClosure:
    TPS_USBPD_closeDevice();
    return (NULL);
}

/* This function will pend on xSemaphore which in turn pends on the IRQ line of I2Ct
    going low. Specifically, this function will wait indefinitely until the CMD1 Complete
    interrupt triggers */
static bool pendOnCMD1Interrupt(void)
{
    uint8_t addrReg;

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
