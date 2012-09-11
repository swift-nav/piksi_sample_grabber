/*
 * Copyright (C) 2012 Swift Navigation Inc.
 * Contact: Fergus Noble <fergus@swift-nav.com>
 *          Colin Beighley <colin@swift-nav.com>
 *
 * This source is subject to the license found in the file 'LICENSE' which must
 * be be distributed together with this source. All other rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <stdio.h>
#include <stdlib.h>

#include "ftd2xx.h"

int main()
{
	FT_STATUS	ft_status;
	FT_HANDLE	ft_handle;
	FT_PROGRAM_DATA eeprom_data;
  DWORD num_devs;
  DWORD VID, PID;
  int iport = 0;

  /* Assign appropriate values to eeprom data */
	eeprom_data.Signature1 = 0x00000000;
	eeprom_data.Signature2 = 0xffffffff;
  eeprom_data.Version = 0; //FT232H
	eeprom_data.VendorId = 0x0403;				
	eeprom_data.ProductId = 0x6014;
	eeprom_data.Manufacturer =  "FTDI";
	eeprom_data.ManufacturerId = "FT";
	eeprom_data.Description = "Piksi UART over USB";
	eeprom_data.SerialNumber = NULL;		// if fixed, or NULL

  //See how many devices are plugged in, fail if more than 1
  printf("Making sure only 1 FTDI device is plugged in\n");
  ft_status = FT_ListDevices(&num_devs,NULL,FT_LIST_NUMBER_ONLY);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to get number of devices, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  } 
  //Check that there is only 1 device plugged in
  if (num_devs > 1){
    fprintf(stderr,"ERROR : More than one FTDI device plugged in\n");
    return EXIT_FAILURE;
  }else if (num_devs == 0){
    fprintf(stderr,"ERROR : No FTDI devices plugged in\n");
    return EXIT_FAILURE;
  }
	
  //Get VID/PID from FTDI device
  printf("Getting VID/PID from device\n");
  ft_status = FT_GetVIDPID(&VID,&PID);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to get VID and PID from FTDI device, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  //Set the VID/PID found
  printf("Setting VID/PID\n");
  ft_status = FT_SetVIDPID(VID,PID);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  //Open the device
  printf("Opening device\n");
  ft_status = FT_Open(iport, &ft_handle);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to open device : ft_status = %d\nHave you tried (sudo rmmod ftdi_sio)?\n",ft_status);
    return EXIT_FAILURE;
  }

  /* Erase the EEPROM to set the device back to UART mode */
  printf("Erasing device EEPROM\n");
  ft_status = FT_EraseEE(ft_handle);
  if(ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Device EEPROM could not be erased : ft_status = %d\n", ft_status);
    return EXIT_FAILURE;
  }

  /* Program device EEPROM */
  printf("Programming device EEPROM\n");
	ft_status = FT_EE_Program(ft_handle, &eeprom_data);
	if(ft_status != FT_OK) {
    fprintf(stderr,"ERROR : Failed to program device EEPROM : ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
	}

  /* Reset the device */
  printf("Resetting device\n");
  ft_status = FT_ResetDevice(ft_handle);
  if(ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Device could not be reset : ft_status = %d\n", ft_status);
    return EXIT_FAILURE;
  }

  /* Close the device */
  printf("Closing device\n");
	FT_Close(ft_handle);
	if(ft_status != FT_OK) {
    fprintf(stderr,"ERROR : Failed to close device : ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
	}

  printf("Unplug and replug your device now\n");
	return 0;
}
