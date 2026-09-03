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
#include "tps_usbpd_i2c_driver.h"

/* USB Structures */
static tModeRegister modeRegister;
static tActiveRDORegister curRDORegister;
static tIntEventRegister curEventRegister;
static tIntEventRegister tmpEventRegister;
static tAutonegotiateSinkRegister curAutoNegRegister;
static tTPS25751WriteCommand curWriteCommand;

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
    uint8_t addrReg;

    /* Call driver init functions and create RTOS objects */
    TPS_USBPD_initializeDevice();
    
    TPS_USBPD_logMessage( "\n--- TPS25751 Change Power Data Object Example ---");

SYSTEM_POWER_ON:
    /* Waiting for USB-PD Source to be plugged in...  */
    TPS_USBPD_logMessage( "Plug in power to TPS25751 to continue...");
    TPS_USBPD_delayMS(500);

    /* Setting up the read transaction to the event register */
    addrReg = TPS25751_MODE_REG;
    while (modeRegister.mode != TPS25751_MODE_APP)
    {
        TPS_USBPD_delayMS(10);
        TPS_USBPD_i2cTransfer(&addrReg, 1, &modeRegister, sizeof(tModeRegister));
    }

    /* Waiting for USB-PD Source to be plugged in...  */
    TPS_USBPD_logMessage( "Device powered! Waiting for USB-PD");
    TPS_USBPD_logMessage( "  Charger negotiation...");

WAIT_FOR_USBPD_CONTRACT:
    /* Only wait for IRQ if not already asserted. If IRQ is asserted, keep executing */
    TPS_USBPD_pendOnIRQ(UINT32_MAX);

    /* Setting up the read transaction to the event register */
    addrReg = TPS25751_INT_EVENT_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &curEventRegister.bytes, sizeof(tIntEventRegister)) == false)
    {
        TPS_USBPD_logMessage( "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    /* Whenever a new contract as consumer happens, verify it is greater than 5V */
    if (curEventRegister.bits.newContractCons == 1)
    {
        TPS_USBPD_logMessage( "\nNew PDO Contract detected!");
        TPS_USBPD_logMessage( "    Reading information then clearing flag...");
        /* Update the current RDO Contract shadow register */
        addrReg = TPS25751_ACTIVE_RDO_REG;
        if(TPS_USBPD_i2cTransfer(&addrReg,  1, &curRDORegister.bytes, sizeof(tActiveRDORegister)) == false)
        {
            TPS_USBPD_logMessage( "USB-PD not responding (NAK)");
            goto TPS25751ErrorClosure;
        }

        /* If there is a new contract event, clear the new contract plug event flag */
        curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
        memset(tmpEventRegister.bytes + 1, 0x00, sizeof(tIntEventRegister) - 1);
        tmpEventRegister.bits.newContractCons = 1;
        memcpy(&curWriteCommand.registerData, &tmpEventRegister, sizeof(tIntEventRegister));
        if(TPS_USBPD_i2cTransfer(&curWriteCommand,  sizeof(tIntEventRegister) + 1, NULL, 0) == false)
        {
            TPS_USBPD_logMessage( "Error clearing interrupt event registers!");
            goto TPS25751ErrorClosure;
        }

        curEventRegister.bits.newContractCons = 0;

        /* Check if requested power object is NOT the first one i.e. (>5V)*/
        if (curRDORegister.bits.objectPosition > 1)
        {
            TPS_USBPD_logMessage( "USB-PD Contract > 5V negotiated!");
            goto POST_INITIAL_USBPD_CONTRACT;
        }

        /* If RDO is the 5V PDO, then nothing to do*/
        if (curRDORegister.bits.objectPosition == 1)
        {
            TPS_USBPD_logMessage( "USB-PD 5V Contract already negotiated!");
            TPS_USBPD_logMessage( "  Nothing left to do...");
            goto TPS25751ErrorClosure;
        }
    }

    /* Clear any remaining interrupts to reset IRQ line */
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    memcpy(&curWriteCommand.registerData, &curEventRegister, sizeof(tIntEventRegister));
    if(TPS_USBPD_i2cTransfer(&curWriteCommand,  sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage( "Error clearing interrupt event registers!");
        goto TPS25751ErrorClosure;
    }

    /* Go back to top of all events cleared and no new Contract as Consumer event set */
    goto WAIT_FOR_USBPD_CONTRACT;

POST_INITIAL_USBPD_CONTRACT:

    /* Clear any remaining interrupts to reset IRQ line */
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
    memcpy(&curWriteCommand.registerData, &curEventRegister, sizeof(tIntEventRegister));
    if(TPS_USBPD_i2cTransfer(&curWriteCommand,  sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage( "Error clearing interrupt event registers!");
        goto TPS25751ErrorClosure;
    }
    
    TPS_USBPD_logMessage( "Reading current autonegotiate sink");
    TPS_USBPD_logMessage( "    register contents...");
    /* Setting up the read transaction to the event register */
    addrReg = TPS25751_AUTONEG_SINK_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg,  1, &curAutoNegRegister.bytes, sizeof(tAutonegotiateSinkRegister)) == false)
    {
        TPS_USBPD_logMessage( "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage( "Changing ");
    TPS_USBPD_logMessage( "  AutonegotiateSink.autoComputeSinkMaxVoltage");
    TPS_USBPD_logMessage( "  to EC-controlled (0x0)");
    curAutoNegRegister.bits.autoComputeSinkMaxVoltage = 0;
    TPS_USBPD_logMessage( "Changing AutonegotiateSink.autoNegMaxVoltage");
    TPS_USBPD_logMessage( "  to 5V (100d)");
    curAutoNegRegister.bits.autoNegMaxVoltage = 100;

    curWriteCommand.writeAddr = TPS25751_AUTONEG_SINK_REG;
    memcpy(&curWriteCommand.registerData, &curAutoNegRegister.bytes, sizeof(tAutonegotiateSinkRegister));
    if(TPS_USBPD_i2cTransfer(&curWriteCommand,  sizeof(tAutonegotiateSinkRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage( "USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_logMessage( "Autonegotiate Sink register setup to request");
    TPS_USBPD_logMessage( "  5V Fixed PDO.");

    TPS_USBPD_logMessage( "Issuing ANeg 4CC command");
    if(TPS_USBPD_i2cTransfer((void*)&autoNegotiateCommand,  sizeof(t4CCCommand), NULL, 0) == false)
    {
        TPS_USBPD_logMessage( "Error issuing 4CC command\n");
        goto TPS25751ErrorClosure;
    }

    TPS_USBPD_delayMS(500);
    goto WAIT_FOR_USBPD_CONTRACT;

TPS25751ErrorClosure:
    TPS_USBPD_closeDevice();
    return (NULL);
}
