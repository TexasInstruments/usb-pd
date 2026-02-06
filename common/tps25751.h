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
#ifndef __TPS25751__H__
#define __TPS25751__H__

#include <stdint.h>

/* Driver Definitions */
#define TPS25751_REG_MAX_PAYLOAD 255

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

/* Sink Capabilities - Write Packet Template */
typedef struct __attribute__((packed)) sTPS25751WriteCommand
{
    uint8_t writeAddr;
    uint8_t registerData[TPS25751_REG_MAX_PAYLOAD];
} tTPS25751WriteCommand;

/* Interrupt Event Register */
typedef union
{
    uint8_t bytes[12];
    struct __attribute__((packed))  
    {
        uint8_t  numOfBytes         : 8;
        uint8_t  reserved0          : 1;
        uint8_t  pdHardReset        : 1;
        uint8_t  reserved1          : 1;
        uint8_t  plugInsertRemoval  : 1;
        uint8_t  powerSwapComplete  : 1;
        uint8_t  dataSwapComplete   : 1;
        uint8_t  reserved2          : 1;
        uint8_t  reserved3          : 1;
        uint8_t  reserved4          : 1;
        uint8_t  overcurrent        : 1;
        uint8_t  reserved5          : 1;
        uint8_t  reserved6          : 1;
        uint8_t  newContractCons    : 1;
        uint8_t  newContractProv    : 1;
        uint8_t  sourceCapRec       : 1;
        uint8_t  sinkCapRec         : 1;
        uint8_t  reserved7          : 1;
        uint8_t  powerSwapReq       : 1;
        uint8_t  dataswapReq        : 1;
        uint8_t  reserved8          : 1;
        uint8_t  usbHostPresent     : 1;
        uint8_t  usbHostNotPresent  : 1;
        uint8_t  reserved9          : 1;
        uint8_t  pwrPathSwChanged   : 1;
        uint8_t  powerStatUpdate    : 1;
        uint8_t  reserved10         : 1;
        uint8_t  statusUpdate       : 1;
        uint8_t  pdStatusUpdate     : 1;
        uint8_t  reserved11         : 2;
        uint8_t  cmd1Complete       : 1;
        uint8_t  reserved12         : 1;
        uint8_t  devIncompError     : 1;
        uint8_t  cannotProvVolCur   : 1;
        uint8_t  canProvVolCurLtr   : 1;
        uint8_t  powerEvent         : 1;
        uint8_t  missingGetCaps     : 1;
        uint8_t  reserved13         : 1;
        uint8_t  protocolError      : 1;
        uint8_t  msgDataError       : 1;
        uint8_t  reserved14         : 2;
        uint8_t  sinkTransComplete  : 1;
        uint8_t  plugEarlyNotf      : 1;
        uint8_t  reserved15         : 2;
        uint8_t  unableToSource     : 1;
        uint16_t  reserved16        : 9;
        uint8_t  extDCDCSinkSafe    : 1;
        uint8_t  extDCDCSourceSafe  : 1;
        uint8_t  reserved17         : 2;
        uint8_t  liquidDetect       : 1;
        uint8_t  reserved18         : 4;
        uint8_t  txMemBuffEmpty     : 1;
        uint8_t  mbrdBuffReady      : 1;
        uint16_t  reserved19        : 13;
        uint8_t  patchLoaded        : 1;
        uint8_t  rdyForPatch        : 1;
        uint8_t  i2cConNacked       : 1;
        uint8_t  reserved20         : 5;
    } bits;
} tIntEventRegister;

/* Status Register */
typedef union 
{
    uint8_t bytes[6];
    struct __attribute__((packed))
    {
        uint8_t  numOfBytes         : 8;
        uint8_t  plugPresent        : 1;
        uint8_t  connectionState    : 3;
        uint8_t  plugOrientation    : 1;
        uint8_t  portRole           : 1;
        uint8_t  dataRole           : 1;
        uint16_t reserved0          : 13;
        uint8_t  vbusStatus         : 2;
        uint8_t  usbHostPresent     : 2;
        uint8_t  actingAsLegacy     : 2;
        uint8_t  reserved1          : 1;
        uint8_t  bist               : 1;
        uint8_t  reserved2          : 2;
        uint8_t  socAckTimeout      : 1;
        uint16_t  reserved3          : 9;
    } bits;
} tStatusRegister;

/* Boot Flags Register */
typedef struct __attribute__((packed)) sBootFlagsRegister 
{
    uint8_t  numOfBytes         : 8;
    uint8_t  patchHeaderError   : 1;
    uint8_t  reserved0          : 1;
    uint8_t  deadBatteryFlag    : 1;
    uint8_t  i2cEEPROMPresent   : 1;
    uint8_t  region0            : 1;
    uint8_t  region1            : 1;
    uint8_t  region0Invalid     : 1;
    uint8_t  region1Invalid     : 1;
    uint8_t  region0EEPROMError : 1;
    uint8_t  region1EEPROMError : 1;
    uint8_t  patchDownloadError : 1;
    uint8_t  reserved1          : 1;
    uint8_t  region0CRCFail     : 1;
    uint8_t  region1CRCFail     : 1;
    uint8_t  reserved2          : 5;
    uint8_t  systemTSD          : 1;
    uint16_t  reserved3         : 9;
    uint8_t  patchConfigSource  : 3;
    uint8_t  revisionID         : 8;
} tBootFlagsRegister;

/* Port Configuration Register */
typedef union 
{
    uint8_t bytes[17];
    struct __attribute__((packed))
    {
        uint8_t  numOfBytes             : 8;
        uint8_t  typCStateMachine       : 2;
        uint8_t  reserved0              : 6;
        uint8_t  typecSupportOpt        : 2;
        uint8_t  disablePD              : 1;
        uint8_t  reserved1              : 5;
        uint8_t vbusOVPUsage            : 2;
        uint8_t  reserved2              : 2;
        uint8_t  ovpForPP5V             : 2;
        uint8_t  reserved3              : 2;
        uint8_t  vbusSyncUVP            : 3;
        uint8_t  apdoVbusUvpThresh      : 2;
        uint8_t  apdoIlimOvershoot      : 2;
        uint8_t  reserved4              : 1;
        uint16_t  apdoVbusUvpTripOff    : 16;
        uint16_t  vbusForValidPPSStat   : 16;
    } bits;
} tPortConfiguration;

/* Patch Burst Mode Data */
typedef union 
{
    uint8_t bytes[7];
    struct __attribute__((packed))
    {
        uint8_t   numOfBytes             : 8;
        uint32_t  lowerRegionSize        : 32;
        uint8_t   i2cTargetAddr          : 7;
        uint8_t   reserved0              : 1;
        uint8_t   timeoutValue           : 6;
        uint8_t   reserved1              : 2;
    } bits;
} tPBMDataReg;

/* Mode Register Structure */
typedef struct __attribute__((packed)) sModeRegister
{
    uint8_t              numOfBytes;
    uint32_t             mode;
} tModeRegister;

/* Mode Register Template */
typedef struct __attribute__((packed)) sCustomerUseRegister
{
    uint8_t              numOfBytes;
    uint32_t             custRegWord1;
    uint32_t             custRegWord2;
} tCustomerUseRegister;

#define TPS25751_PBM_DATA_PAYLOAD_SIZE 6

/* Mode Definitions */
#define TPS25751_MODE_APP 0x20505041
#define TPS25751_MODE_PTCH 0x48435450

/* Register Addresses */
#define TPS25751_MODE_REG            0x03
#define TPS25751_CUST_USE_REG        0x06
#define TPS25751_4CC_REG             0x08
#define TPS25751_CMD1_DATA_REG       0x09
#define TPS25751_INT_EVENT_REG       0x14
#define TPS25751_INT_EVENT_MASK_REG  0x16
#define TPS25751_INT_EVENT_CLR_REG   0x18
#define TPS25751_STATUS_EVENT_REG    0x1A
#define TPS25751_PORT_CONFIG_REG     0x26
#define TPS25751_SOURCE_CAP_REG      0x30
#define TPS25751_SINK_CAP_REG        0x33
#define TPS25751_BOOT_FLAGS_REG      0x2D

/* 4CC Command Template */
typedef struct __attribute__((packed)) s4CCCommand
{
    uint8_t              commandRegister;
    uint8_t              numOfBytes;
    uint8_t              fourCCBytes[4];
} t4CCCommand;

/* PBMs Response*/
typedef union 
{
    uint8_t bytes[64];
    struct __attribute__((packed)) 
    {
        uint8_t              numOfBytes;
        uint8_t              status;
    } bits;
} tPBMsResponse;

#define TPS25751_4CC_GSrc_CMD {0x47, 0x53, 0x72, 0x43}
#define TPS25751_4CC_DBfg_CMD {0x44, 0x42, 0x66, 0x67}
#define TPS25751_4CC_PBMs_CMD {0x50, 0x42, 0x4D, 0x73}
#define TPS25751_4CC_PBMc_CMD {0x50, 0x42, 0x4D, 0x63}

#endif
