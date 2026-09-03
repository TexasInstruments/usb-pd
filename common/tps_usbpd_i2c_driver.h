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
#ifndef __TPS_I2C_DRIVER__H
#define __TPS_I2C_DRIVER__H

#include <stdint.h>
#include <stdbool.h>

/* --------------------------- Porting Functions ---------------------------- */
/*
 * These functions should be implemented by each individual device port. They
 * handle the low-level hardware specific implementation with the respective
 * architecture's specific hardware peripherals (I2C)
 */
 
/**
 *  @brief Performs a simple I2C transaction.
 *
 *  This API simply issues an I2C transaction to the I2C address that was
 *  configured by either TPS_USBPD_initializeDevice or the 
 *  TPS_USBPD_setI2CTargetAddress function. This function should be able to
 *  do a write-only transaction, read-only transaction, or a write, repeated
 *  start, read command. If a write-only or read-only transaction is performed,
 *  a NULL value should be specified for the pointer not being used and a 0
 *  value should be provided to the respective length parameter. 
 *
 *  @param[in]  writeData Pointer to write data
 *  @param[in]  writeLen  Length of data to write
 *  @param[in]  readData  Pointer to read data
 *  @param[in]  readLen   Length of data to read
 *
 *  @retval returnCode true if successful, false otherwise
 *
 */
extern bool TPS_USBPD_i2cTransfer(void* writeData, size_t writeLen,
                                  void* readData, size_t readLen);
                                  
/**
 *  @brief Sets the I2C target address of the USB-PD controller
 *
 *  Sets the target I2C address to use. In the TPS_USBPD_initializeDevice
 *  function, the default target address is set to the 
 *  TPS_USBPD_I2C_DEFAULT_TARGET_ADDR predefine that is defined in this
 *  header file, however sometimes (such as using PBM commands) there exists
 *  a need to change the I2C target address after initial configuration.
 *
 *  @param[in]  addr I2C target address to set
  *
 */
extern void TPS_USBPD_setI2CTargetAddress(uint8_t addr);

/**
 *  @brief Prints string to user's log outlet of choice
 *
 *  Logs a message to the user's log outlet of choice. This can be an empty
 *  function if logging is not needed. This function mimics printf
 *  functionality.
 *
 *  @param[in] Formatted print string to log
 *
 */
extern void TPS_USBPD_logMessage(const char *restrict format, ...);

/**
 *  @brief Initializes all driver/RTOS objects
 *
 *  This API should initialize all hardware/RTOS objects needed for the 
 *  specific driver implementation to operate correctly. This function should
 *  take care of hardware initialization such as I2C/GPIOs, as well as any
 *  relevant software configuration such as RTOS. This function should be
 *  called at the start of any application.
 *
 *  @retval returnCode true if successful, false otherwise
 *
 */
extern bool TPS_USBPD_initializeDevice(void);

/**
 *  @brief Closes all relevant driver/RTOS objects. 
 *
 *  This function should be called at the end of any application to close and
 *  free any resources allocated by the TPS_USBPD_initializeDevice function.
 *
 */
extern void TPS_USBPD_closeDevice(void);

/**
 *  @brief Performs a blocking millisecond delay
 *
 *  Blocking function that delays for the specified number of milliseconds.
 *  This function will generally call an RTOS sleep/delay function.
 *
 *  @param[in]  delayInMS How long, in milliseconds, to delay
 *
 */
extern void TPS_USBPD_delayMS(uint32_t delayInMS);

/**
 *  @brief Pend on falling edge of I2Ct_IRQ line
 *
 *  Function that blocks (with timeout) on the falling edge of the I2Ct_IRQ line
 *  from the microcontroller. This can be implemented using an RTOS or
 *  semaphore or with bare metal.
 *
 *  @param[in]  timeoutValue Timeout value to pend, implementation specific.
 *
 *  @retval true if successful, false otherwise
 *
 */
extern bool TPS_USBPD_pendOnIRQ(uint32_t timeoutValue);

/* Device Specific Variables */
#define TPS_USBPD_I2C_DEFAULT_TARGET_ADDR     0x21

#endif
