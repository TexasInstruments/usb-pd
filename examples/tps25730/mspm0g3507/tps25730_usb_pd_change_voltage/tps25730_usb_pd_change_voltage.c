/*
 * Copyright (c) 2025, Texas Instruments Incorporated
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
#include <math.h>

/* USB Configuration */
#include "tps25730.h"
#include "tps_usbpd_i2c_driver.h"

/* USB Structures */
static tSinkSourceCapabilities sinkCapabilities;
static tSinkSourceWritePacket sinkReadPacket;
const t4CCCommand gSrcCommand = 
{
    .commandRegister = TPS25730_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = TPS25730_4CC_GSrc_CMD
};

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    uint8_t addrReg;

    /* Call driver init functions and create RTOS objects */
    TPS_USBPD_initializeDevice();
    TPS_USBPD_setI2CTargetAddress(0x20);

    TPS_USBPD_logMessage("--- TPS25730 Code Example ---\n");

    /* Reading the PDOs initially */
    addrReg = TPS25730_SINK_CAP_REG;
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &sinkCapabilities, sizeof(sinkCapabilities)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        while (1)
        {
        }
    }

    TPS_USBPD_logMessage("---PDO1 Parameters (Before) ---\n");
    TPS_USBPD_logMessage("Min Voltage: %fV\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.minimumVoltage * 0.05f);
    TPS_USBPD_logMessage("Max Voltage: %fV\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.maximumVoltage * 0.05f);
    TPS_USBPD_logMessage("Current: %fA\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.operationalCurrent * 0.01f);

    /* Changing the voltage to 9V (9/.05 = 180)*/
    sinkCapabilities.sinkPDOs[0].bits.maximumVoltage = 180;
    sinkReadPacket.writeAddr = TPS25730_SINK_CAP_REG;
    memcpy(&sinkReadPacket.sinkSourceCap, &sinkCapabilities, sizeof(tSinkSourceCapabilities));
    if(TPS_USBPD_i2cTransfer(&sinkReadPacket, sizeof(tSinkSourceCapabilities) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        while (1)
        {
        }
    }

    TPS_USBPD_logMessage("Updated PDO1 maximum voltage to 9V.\n");

    /* Issuing the 4CC command to redo the source capabilities */
   if(TPS_USBPD_i2cTransfer((void*)&gSrcCommand, sizeof(t4CCCommand), NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        while (1)
        {
        }
    }

    TPS_USBPD_logMessage("Issued GSrC 4CC command.\n");

    /* Reading the PDOs back to make sure it was changed */
    if(TPS_USBPD_i2cTransfer(&addrReg, 1, &sinkCapabilities, sizeof(sinkCapabilities)) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        while (1)
        {
        }
    }

    TPS_USBPD_logMessage("---PDO1 Parameters (After) ---\n");
    TPS_USBPD_logMessage("Min Voltage: %fV\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.minimumVoltage * 0.05f);
    TPS_USBPD_logMessage("Max Voltage: %fV\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.maximumVoltage * 0.05f);
    TPS_USBPD_logMessage("Current: %fA\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.operationalCurrent * 0.01f);

    TPS_USBPD_closeDevice();

    return (NULL);
}
