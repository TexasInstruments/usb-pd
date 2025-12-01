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
#ifndef __TI_USB_PDO
#define __TI_USB_PDO

#include <stdint.h>

/* Variable PDO Structure */
typedef union 
{
    uint32_t word;
    struct __attribute__((packed)) 
    {
        uint16_t operationalCurrent : 10;
        uint16_t minimumVoltage     : 10;
        uint16_t maximumVoltage     : 10;
        uint8_t  variableSupply     : 2;
    } bits;
} TI_USB_VARIABLE_PDO;

/* Fixed PDO Structure */
typedef union 
{
    uint32_t word;
    struct __attribute__((packed)) 
    {
        uint16_t operationalCurrent : 10;
        uint16_t operationalVoltage : 10;
        uint16_t reserved1          : 5;
        uint8_t  dualRoleData       : 1;
        uint8_t  reserved2          : 2;
        uint8_t  higherCapability   : 1;
        uint8_t  dualRolePower      : 1;
        uint8_t  suuplyType         : 2;
    } bits;
} TI_USB_FIXED_PDO;

/* Sink Capabilities Register */
typedef struct __attribute__((packed)) sSinkSourceCapabilities
{
    uint8_t              numOfBytes;
    uint8_t              numOfPDOs   :   3;
    uint8_t              reserved0   :   5;
    TI_USB_FIXED_PDO     fixedPDO;
    TI_USB_VARIABLE_PDO  sinkPDOs[6];
    uint8_t              reserved1[24];
} tSinkSourceCapabilities;

    
/* Write Packet Template */
typedef struct __attribute__((packed)) sSinkSourceWritePacket
{
    uint8_t writeAddr;
    tSinkSourceCapabilities sinkSourceCap;

} tSinkSourceWritePacket;

/* Register Addresses */
#define TPS25730_SOURCE_CAP_REG 0x30
#define TPS25730_SINK_CAP_REG   0x33
#define TPS25730_4CC_REG        0x08

/* 4CC Command Template */
typedef struct __attribute__((packed)) s4CCCommand
{
    uint8_t              commandRegister;
    uint8_t              numOfBytes;
    uint8_t              fourCCBytes[4];
} t4CCCommand;

#endif
