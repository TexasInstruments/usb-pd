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

/* Driver Header files */
#include <ti/display/DisplayUart.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/I2C.h>

/* Driver configuration */
#include "ti_drivers_config.h"

/* USB Configuration */
#include "usb_pdo.h"

/* I2C target addresses */
const uint8_t i2cTargetAddr = 0x20;

/* Structures for driver development */
static Display_Handle display;

/* USB Structures */
static tSinkSourceCapabilities sinkCapabilities;
static tSinkSourceWritePacket sinkReadPacket;
const t4CCCommand gSrcCommand = 
{
    .commandRegister = TPS25730_4CC_REG,
    .numOfBytes = 4,
    .fourCCBytes = {0x47, 0x53, 0x72, 0x43}
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

    /* Call driver init functions */
    Display_init();
    I2C_init();

    /* Open the UART display for output */
    display = Display_open(Display_Type_UART, NULL);
    if (display == NULL)
    {
        while (1)
        {
        }
    }

    Display_printf(display, 0, 0, "--- TPS25730 Code Example ---\n");

    /* Create I2C for usage */
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;
    i2c               = I2C_open(CONFIG_I2C_TMP, &i2cParams);
    if (i2c == NULL)
    {
        Display_printf(display, 0, 0, "Error Initializing I2C\n");
        while (1)
        {
        }
    } else
    {
        Display_printf(display, 0, 0, "I2C Initialized!\n");
    }

    /* Reading the PDOs initially */
    addrReg = TPS25730_SINK_CAP_REG;
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &sinkCapabilities;
    i2cTransaction.readCount  = sizeof(sinkCapabilities);
    i2cTransaction.targetAddress = i2cTargetAddr;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "Error reading source PDOs\n");
        while (1)
        {
        }
    }

    Display_printf(display, 0, 0, "---PDO1 Parameters (Before) ---\n");
    Display_printf(display, 0, 0, "Min Voltage: %fV\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.minimumVoltage * 0.05f);
    Display_printf(display, 0, 0, "Max Voltage: %fV\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.maximumVoltage * 0.05f);
    Display_printf(display, 0, 0, "Current: %fA\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.operationalCurrent * 0.01f);

    /* Changing the voltage to 9V (9/.05 = 180)*/
    sinkCapabilities.sinkPDOs[0].bits.maximumVoltage = 180;
    sinkReadPacket.writeAddr = TPS25730_SINK_CAP_REG;
    memcpy(&sinkReadPacket.sinkSourceCap, &sinkCapabilities, sizeof(tSinkSourceCapabilities));
    i2cTransaction.writeCount = sizeof(tSinkSourceCapabilities) + 1;
    i2cTransaction.writeBuf = &sinkReadPacket;
    i2cTransaction.readCount = 0;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "Error writing sink PDOs\n");
        while (1)
        {
        }
    }

    Display_printf(display, 0, 0, "Updated PDO1 maximum voltage to 9V.\n");

    /* Issuing the 4CC command to redo the source capabilities */
    i2cTransaction.writeBuf = (void*)&gSrcCommand;
    i2cTransaction.writeCount = sizeof(t4CCCommand);

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "Error issuing 4CC command\n");
        while (1)
        {
        }
    }

    Display_printf(display, 0, 0, "Issued GSrC 4CC command.\n");

    /* Reading the PDOs back to make sure it was changed */
    i2cTransaction.writeBuf   = &addrReg;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf    = &sinkCapabilities;
    i2cTransaction.readCount  = sizeof(sinkCapabilities);
    i2cTransaction.targetAddress = i2cTargetAddr;

    if (I2C_transfer(i2c, &i2cTransaction) == false)
    {
        Display_printf(display, 0, 0, "Error reading sink PDOs\n");
        while (1)
        {
        }
    }

    Display_printf(display, 0, 0, "---PDO1 Parameters (After) ---\n");
    Display_printf(display, 0, 0, "Min Voltage: %fV\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.minimumVoltage * 0.05f);
    Display_printf(display, 0, 0, "Max Voltage: %fV\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.maximumVoltage * 0.05f);
    Display_printf(display, 0, 0, "Current: %fA\n", (float_t)sinkCapabilities.sinkPDOs[0].bits.operationalCurrent * 0.01f);

    I2C_close(i2c);
    Display_printf(display, 0, 0, "I2C closed!");
    return (NULL);
}
