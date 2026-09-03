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

/* Functions/Structures for driver development */
static void printInterruptStatus(tIntEventRegister* intEvent);

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
    uint8_t addrReg;
    uint8_t sizeWritten;
    uint32_t ii;
    tModeRegister modeReg;

    /* Create RTOS objects and initializing device */
    TPS_USBPD_initializeDevice();

    TPS_USBPD_logMessage("\n--- TPS25751A Interrupt Monitor ---");

    /* Waiting for the device to be in APP mode  */
    modeReg.mode = 0;
    addrReg = TPS25751_MODE_REG;

    TPS_USBPD_logMessage("Waiting for device to be in APP mode...");

    while (modeReg.mode != TPS25751_MODE_APP)
    {
        TPS_USBPD_delayMS(10);
        TPS_USBPD_i2cTransfer(&addrReg, 1, &modeReg, sizeof(tModeRegister));
    }

    TPS_USBPD_logMessage("    device booted into APP mode!");

    do
    {
        /* Reading out interrupt */
        addrReg = TPS25751_INT_EVENT_REG;
        if(TPS_USBPD_i2cTransfer(&addrReg, 1,
                                &curTriggeredIntsReg,
                                sizeof(tIntEventRegister)) == false)
        {
            TPS_USBPD_delayMS(100);
            continue;
        }

        /* Clearing the interrupts */
        memcpy(&curWriteCommand.registerData, &curTriggeredIntsReg.bytes,
                 sizeof(tIntEventRegister));
        curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
        if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tIntEventRegister) + 1,
                                NULL, 0) == false)
        {
            TPS_USBPD_logMessage("USB-PD not responding (NAK)");
            goto TPS25751ErrorClosure;
        }

        /* Dumping the relevant information to the terminal */
        printInterruptStatus(&curTriggeredIntsReg);

        TPS_USBPD_logMessage("\nPending on I2Ct_IRQ...");
        TPS_USBPD_pendOnIRQ(UINT32_MAX);

    } while(1);  
    
TPS25751ErrorClosure:
    TPS_USBPD_closeDevice();
    return (NULL);
}

static void printInterruptStatus(tIntEventRegister* intEvent)
{
    TPS_USBPD_logMessage("--- INT_EVENT ---");
    TPS_USBPD_logMessage("   pdHardReset = %x", intEvent->bits.pdHardReset);
    TPS_USBPD_logMessage("   plugInsertRemoval = %x", intEvent->bits.plugInsertRemoval);
    TPS_USBPD_logMessage("   powerSwapComplete = %x", intEvent->bits.powerSwapComplete);
    TPS_USBPD_logMessage("   dataSwapComplete = %x", intEvent->bits.dataSwapComplete);
    TPS_USBPD_logMessage("   overcurrent = %x", intEvent->bits.overcurrent);
    TPS_USBPD_logMessage("   newContractCons = %x", intEvent->bits.newContractCons);
    TPS_USBPD_logMessage("   newContractProv = %x", intEvent->bits.newContractProv);
    TPS_USBPD_logMessage("   sourceCapRec = %x", intEvent->bits.sourceCapRec);
    TPS_USBPD_logMessage("   sinkCapRec = %x", intEvent->bits.sinkCapRec);
    TPS_USBPD_logMessage("   powerSwapReq = %x", intEvent->bits.powerSwapReq);
    TPS_USBPD_logMessage("   dataswapReq = %x", intEvent->bits.dataswapReq);
    TPS_USBPD_logMessage("   usbHostPresent = %x", intEvent->bits.usbHostPresent);
    TPS_USBPD_logMessage("   usbHostNotPresent = %x", intEvent->bits.usbHostNotPresent);
    TPS_USBPD_logMessage("   pwrPathSwChanged = %x", intEvent->bits.pwrPathSwChanged);
    TPS_USBPD_logMessage("   powerStatUpdate = %x", intEvent->bits.powerStatUpdate);
    TPS_USBPD_logMessage("   statusUpdate = %x", intEvent->bits.statusUpdate);
    TPS_USBPD_logMessage("   pdStatusUpdate = %x", intEvent->bits.pdStatusUpdate);
    TPS_USBPD_logMessage("   cmd1Complete = %x", intEvent->bits.pdStatusUpdate);
    TPS_USBPD_logMessage("   devIncompError = %x", intEvent->bits.devIncompError);
    TPS_USBPD_logMessage("   cannotProvVolCur = %x", intEvent->bits.cannotProvVolCur);
    TPS_USBPD_logMessage("   canProvVolCurLtr = %x", intEvent->bits.canProvVolCurLtr);
    TPS_USBPD_logMessage("   powerEvent = %x", intEvent->bits.powerEvent);
    TPS_USBPD_logMessage("   missingGetCaps = %x", intEvent->bits.missingGetCaps);
    TPS_USBPD_logMessage("   protocolError = %x", intEvent->bits.protocolError);
    TPS_USBPD_logMessage("   msgDataError = %x", intEvent->bits.msgDataError);
    TPS_USBPD_logMessage("   sinkTransComplete = %x", intEvent->bits.sinkTransComplete);
    TPS_USBPD_logMessage("   plugEarlyNotf = %x", intEvent->bits.plugEarlyNotf);
    TPS_USBPD_logMessage("   unableToSource = %x", intEvent->bits.unableToSource);
    TPS_USBPD_logMessage("   extDCDCSinkSafe = %x", intEvent->bits.extDCDCSinkSafe);
    TPS_USBPD_logMessage("   extDCDCSourceSafe = %x", intEvent->bits.extDCDCSourceSafe);
    TPS_USBPD_logMessage("   liquidDetect = %x", intEvent->bits.liquidDetect);
    TPS_USBPD_logMessage("   txMemBuffEmpty = %x", intEvent->bits.txMemBuffEmpty);
    TPS_USBPD_logMessage("   mbrdBuffReady = %x", intEvent->bits.mbrdBuffReady);
    TPS_USBPD_logMessage("   patchLoaded = %x", intEvent->bits.patchLoaded);
    TPS_USBPD_logMessage("   rdyForPatch = %x", intEvent->bits.rdyForPatch);
    TPS_USBPD_logMessage("   i2cConNacked = %x", intEvent->bits.i2cConNacked);
}

