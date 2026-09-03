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
    uint8_t addrReg;

    /* Call driver init functions and create RTOS objects */
    TPS_USBPD_initializeDevice();

    TPS_USBPD_logMessage("\n--- TPS25751 Dead Battery Code Example ---");

    TPS_USBPD_logMessage("Waiting for I2C interrupt...");
DEAD_BATTERY_TASK_START:

    /* Waiting for the interrupt event */
    TPS_USBPD_pendOnIRQ(UINT32_MAX);

    /* Setting up the read transaction to the event register */
    addrReg = TPS25751_INT_EVENT_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &curEventRegister.bytes, sizeof(tIntEventRegister)) == false)
    {
        goto DEAD_BATTERY_TASK_START;
    }

    /* Seeing if there was a plug event  */
    if(curEventRegister.bits.plugInsertRemoval == 1)
    {
        TPS_USBPD_logMessage("Plug event detected! Clearing flag...");

        /* If there is a plug event, clear the plug event flag */
        curWriteCommand.writeAddr = TPS25751_INT_EVENT_CLR_REG;
        memcpy(&curWriteCommand.registerData, &curEventRegister, sizeof(tIntEventRegister));
        if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tIntEventRegister) + 1, NULL, 0) == false)
        {
            TPS_USBPD_logMessage("Error clearing interrupt event registers!");
            goto DEAD_BATTERY_TASK_START;
        }

        curEventRegister.bits.plugInsertRemoval = 0;
        
        TPS_USBPD_logMessage("Reading boot flags register...");
        addrReg = TPS25751_BOOT_FLAGS_REG;
        if(TPS_USBPD_i2cTransfer(&addrReg, 1, &curBootFlagRegister, sizeof(tBootFlagsRegister)) == false)
        {
            TPS_USBPD_logMessage("USB-PD not responding (NAK)");
            goto DEAD_BATTERY_TASK_START;
        }

        if(curBootFlagRegister.bits.deadBatteryFlag == 1)
        {
            TPS_USBPD_logMessage("Dead battery flag detected!");
        }
        else
        {
            TPS_USBPD_logMessage("Dead battery flag not detected!");
            goto DEAD_BATTERY_TASK_START;
        }

        /* Issuing DBfg command */
        TPS_USBPD_logMessage("Issuing DBfg 4CC command");
        if(TPS_USBPD_i2cTransfer((void*)&deadBatteryClearCommand, sizeof(t4CCCommand), NULL, 0) == false)
        {
            TPS_USBPD_logMessage("USB-PD not responding (NAK)");
            goto DEAD_BATTERY_TASK_START;
        }

        /* Otherwise, sleep for a bit and read back the boot register to confirm dead battery
            flag was cleared */
        TPS_USBPD_delayMS(50);
        addrReg = TPS25751_BOOT_FLAGS_REG;
        if(TPS_USBPD_i2cTransfer(&addrReg, 1, &curBootFlagRegister, sizeof(tBootFlagsRegister)) == false)
        {
            TPS_USBPD_logMessage("USB-PD not responding (NAK)");
            goto DEAD_BATTERY_TASK_START;
        }

        if(curBootFlagRegister.bits.deadBatteryFlag == 0)
        {
            /* Set a breakpoint here to demonstrate functionality. */
            TPS_USBPD_logMessage("Dead battery flag cleared successfully!");
        }
        else
        {
            TPS_USBPD_logMessage("Dead battery flag not cleared!");
            goto DEAD_BATTERY_TASK_START;
        }
    }
    else
    {
        goto DEAD_BATTERY_TASK_START;
    }

    TPS_USBPD_closeDevice();
    return (NULL);
}
