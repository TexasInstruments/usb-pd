<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://www.ti.com/content/dam/ticom/images/identities/ti-brand/ti-logo-hz-1c-white.svg" width="300">
  <img alt="Texas Instruments Logo" src="https://www.ti.com/content/dam/ticom/images/identities/ti-brand/ti-hz-2c-pos-rgb.svg" width="300">
</picture>

# TPS25751 Load Configuration from MCU Flash

## Summary

This code example shows how to load a patch configuration to the [TPS25751](https://www.ti.com/product/TPS25751) directly from the MCU flash without the need for any external EEPROM. This flow is roughly based off the flow from the [Using an Embedded Controller (EC) to Load a Patch Bundle Directly to the TPS25751 or TPS26750 ](https://www.ti.com/lit/pdf/slvafv8) application note, however uses the [MSPM0G3507](https://www.ti.com/product/MSPM0G3507) microcontroller in combination with FreeRTOS/TI-Drivers to manage the communication. 

## Hardware Configuration

The [TPS25751EVM](https://www.ti.com/tool/TPS25751EVM) is used with the [LP-MSPM0G3507 LaunchPad](https://www.ti.com/tool/LP-MSPM0G3507). The I2C lines are connected via jumper wire with the MSPM0G3507 being the I2C controller and the TPS25751 being the I2C peripheral device. The jumper configuration can be seen below:

##### **[TPS25751EVM](https://www.ti.com/tool/TPS25751EVM)**

![TPS25730EVM](../../../../doc/751.jpg "TPS25751EVM Connections")

##### **[LP-MSPM0G3507](https://www.ti.com/tool/LP-MSPM0G3507)**

![TPS25730EVM](../../../../doc/m0orange.jpg "MSPM0 Connections")

In this configuration, the green wire is I2C data (SDA), the red wire is I2C clock (SCL), the orange wire is I2C interrupt, and the yellow wire is ground (GND).  Also note that PB24 is used for I2C interrupts so the jumper J9 must be removed on the MSPM0 LaunchPad.

Also note that in order to disable the EEPROM, the jumper **J10** on the TPS25751EVM must be removed. 

Other hardware configurations and platforms are possible to use. Please refer to the [README.md](https://github.com/TexasInstruments/usb-pd) in the repository root for how to connect the TPS25751 to a different MCU platform.

## Build Instructions

Please refer to the build instructions included in the root of the examples repository [README.md](https://github.com/TexasInstruments/usb-pd).

## Usage

Note that the device configuration file that is used to setup the TPS25751EVM has been checked into this repository in the [config.json](https://github.com/TexasInstruments/usb-pd/blob/main/examples/tps25751/mspm0g3507/tps25751_patch_load_from_mcu/config.json) file. You can use this JSON file with the [USB Configuration Tool](https://dev.ti.com/gallery/view/USBPD/USBCPD_Application_Customization_Tool/) as described in the [TPS25751EVM's User's Guide](https://www.ti.com/lit/pdf/SLVUCP9).

The patch image from this code example (stored in **[lbr.c](./lbr.c)**) was generated from the same USB configuration tool. To generate a C header file to load to the TPS25751, go to the **Export->Generate low region binary** option of the configuration tool:

![TPS25751EVM](./doc/lbr.png "Low Region Binary")

In order to verify that the patch was loaded to the TPS25751 correctly, the customer use registers are set to special values via the configuration tool:
![Special Values](./doc/beef.png "Special Values")

After the patch is loaded, this register is read to ensure that the correct values have been persisted.

This code example takes the register structures of the TPS25751's host interface (as described in the [TPS25751 Technical User's Manual](https://www.ti.com/lit/pdf/slvucr8)) and represents them in a standard C header file. The status register, for example:

![TPS25751EVM](./doc/statusreg.png "Status")

... is mapped pragmatically to a header file as seen below from **[tps25751.h](https://github.com/TexasInstruments/usb-pd/blob/main/common/tps25751.h)**:

```c
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
        uint16_t  reserved3         : 9;
    } bits;
} tStatusRegister;
```

Using these header files, this code example keeps a "shadow" copy of the device's configuration in RAM and shows how to keep track of the device's interrupt events registers. In this code example, we setup the MCU to listen for a falling edge interrupt on the I2C IRQ line. This is done periodically throughout the code to prevent polling the I2C line and waiting for certain events to take place:

```c
    /* Waiting for CMD1 interrupt */
WaitForPMBSCMD:
    TPS_USBPD_pendOnIRQ(UINT32_MAX);
```

After booting up, the device periodically reads the MODE register and waits for it to return that the device is in PTCH mode:

```c
    /* Waiting for the device to be in PTCH mode  */
    modeReg.mode = 0;
    addrReg = TPS25751_MODE_REG;
    TPS_USBPD_logMessage("Waiting for device to be in PTCH mode...");
    while (modeReg.mode != TPS25751_MODE_PTCH)
    {
        TPS_USBPD_delayMS(10);
        TPS_USBPD_i2cTransfer(&addrReg, 1, &modeReg, sizeof(tModeRegister));
    }
```

Throughout the code example, the **CMD1 complete** interrupt is used for synchronization. As such, the interrupt mask is enabled so that the IRQ line toggles accordingly:

```c
    /* Setting interrupt mask to enable CMD1 complete */
    TPS_USBPD_logMessage("Enabling CMD1 interrupts...");
    curWriteCommand.writeAddr = TPS25751_INT_EVENT_MASK_REG;
    memcpy(&curWriteCommand.registerData, &curEventRegister.bytes, sizeof(tIntEventRegister));
   
    if(TPS_USBPD_i2cTransfer(&curWriteCommand, sizeof(tIntEventRegister) + 1, NULL, 0) == false)
    {
        TPS_USBPD_logMessage("USB-PD not responding (NAK)");
        goto TPS25751ErrorClosure;
    }
```

The first part of the patch load process is to issue the PBMs 4CC command. Before doing this, however, the relevant data needs to be set to the CMD Data register. This data has been prepoulated in the defined structure at the top of the file:

```c
static tPBMDataReg curPBMDataReg = 
{
    .bits.numOfBytes = TPS25751_PBM_DATA_PAYLOAD_SIZE,
    .bits.i2cTargetAddr = TPS25751_BURST_REG,
    .bits.timeoutValue = TPS25751_PBMS_TIMEOUT
};
```

Note that the image size is populated during runtime when the PBMs data is persisted to avoid link-time restrictions:

```c
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
```

After the PBMs data has been persisted to the device, we are ready to issue the PBMs 4CC command and start to pipe in the patch data. Below, the PBMs command is issued and we wait for an interrupt to signal that we can start to pipe data to the device:

```c
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
```

After verifying that the command went through successfully, we transfer the patch data in one big I2C transaction. Note that we set the new I2C target address to the one that matches the PBM configuration data:

```c
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
```

After a short delay and verification that the command was executed, we pend on the IRQ semaphore to make sure that the firmware patch was loaded correctly. From there, we poll the MODE register to ensure that we have entered APP mode successfully:

```c
    TPS_USBPD_logMessage("Waiting for device to be in APP mode...");
    addrReg = TPS25751_MODE_REG;
    modeReg.mode = 0;  
    while (modeReg.mode != TPS25751_MODE_APP)
    {
        TPS_USBPD_delayMS(10);
        TPS_USBPD_i2cTransfer(&addrReg, 1, &modeReg, sizeof(tModeRegister));
    }

    TPS_USBPD_logMessage("Device is in APP, patch loaded!");
```

The final step of the program is to read the customer user register to ensure that the values we set in the initial step through the configuration tool persisted correctly to the patch:

```c
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
```

The output of the terminal can be seen below:
![Terminal](./doc/terminal.png "Terminal")

The full [Saleae](https://saleae.com/) logic trace can be found below:
[logic.sal](https://github.com/TexasInstruments/usb-pd/blob/main/examples/tps25751/mspm0g3507/tps25751_patch_load_from_mcu/logic.sal)

## Licensing

See [LICENSE.md](https://github.com/TexasInstruments/usb-pd/blob/main/LICENSE)

---

## Developer Resources

[TI E2E™ design support forums](https://e2e.ti.com) | [Learn about software development at TI](https://www.ti.com/design-development/software-development.html) | [Training Academies](https://www.ti.com/design-development/ti-developer-zone.html#ti-developer-zone-tab-1) | [TI Developer Zone](https://dev.ti.com/)
