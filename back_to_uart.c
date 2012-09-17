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
#include <getopt.h>

#include "ftd2xx.h"

void print_usage()
{
  printf("Usage: back_to_uart [options]\n"
         "Options:\n"
         "  [-p]  Don't prompt user as to whether device being written to is correct.\n"
         "  [-h]  Print this information.\n"
  );
}

int main(int argc, char *argv[])
{

  int dont_prompt = 0;

  static const struct option long_opts[] = {
    {NULL, no_argument, NULL, 'p'},
    {NULL, no_argument, NULL, 'h'},
    {NULL, no_argument, NULL, 0}
  };

  opterr = 0;
  int c;
  int option_index = 0;
  while ((c = getopt_long(argc, argv, "ph", long_opts, &option_index)) != -1)
    switch (c) {
      case 'p':
        dont_prompt++;
        break;
      case 'h':
        print_usage();
        return EXIT_SUCCESS;
      case '?':
        fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        return EXIT_FAILURE;
      default:
        abort();
     }

	FT_STATUS	ft_status;
	FT_HANDLE	ft_handle;
//	FT_PROGRAM_DATA eeprom_data;
  DWORD num_devs;
  DWORD VID, PID;
  int iport = 0;

  //See how many devices are plugged in, fail if greater than 1
  printf("Creating device info list\n");
  ft_status = FT_CreateDeviceInfoList(&num_devs);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to create device info list, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  printf("Making sure only one FTDI device is plugged in\n");
  //Check that there aren't more than 1 device plugged in
  if (num_devs > 1){
    fprintf(stderr,"ERROR : More than one FTDI device plugged in\n");
    return EXIT_FAILURE;
  }
	
  //Get VID/PID from FTDI device
  printf("Getting VID/PID from device\n");
  ft_status = FT_GetVIDPID(&VID,&PID);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to get VID and PID from FTDI device, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  printf("    VID = %04x, PID = %04x\n",VID,PID);
  //Set the VID/PID found
  printf("Setting VID/PID\n");
  ft_status = FT_SetVIDPID(VID,PID);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }

  //Get info about the device and print to user
  DWORD lpdw_flags;
  DWORD lpdw_type;
  DWORD lpdw_id;
  char pc_serial_number[16];
  char pc_description[64];
  ft_status = FT_GetDeviceInfoDetail(0,&lpdw_flags,&lpdw_type,&lpdw_id,NULL,pc_serial_number,pc_description,&ft_handle);
  printf("Device Information : \n");
  printf("     Description   : %s\n", pc_description);
  printf("     Serial Number : %s\n", pc_serial_number);
  printf("     Flags         : 0x%x\n", lpdw_flags);
  printf("     Type          : 0x%x\n", lpdw_type);
  printf("     ID            : 0x%x\n", lpdw_id);
//  if (ft_status != FT_OK){
//    fprintf(stderr,"ERROR : Failed to get device info, ft_status = %d\n",ft_status);
//    return EXIT_FAILURE;
//  }

  //Ask user if this is the correct device
  if (dont_prompt == 0) {
    char correct_device;
    printf("Is this the correct device? (y/n) : ");
    scanf("%c",&correct_device);
    while ((correct_device != 'y') && (correct_device != 'n')) {
      printf("\rPlease enter y or n");
      scanf("%c",&correct_device);
    }
    if (correct_device == 'n'){
      printf("Exiting, since this is not the device we want to program\n");
      return EXIT_SUCCESS;
    }
  }

  //Open the device
  printf("Attempting to open device using read VID/PID...");
  ft_status = FT_Open(iport, &ft_handle);
  //If that didn't work, try some other likely VID/PID combos
  //Try 0403:8398
  if (ft_status != FT_OK){
    printf("FAILED\nTrying VID=0x0403, PID=0x8398...");
    ft_status = FT_SetVIDPID(0x0403,0x8398);
    if (ft_status != FT_OK){
      fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
      return EXIT_FAILURE;
    }
    ft_status = FT_Open(iport, &ft_handle);
  }
  //Try 0403:6014
  if (ft_status != FT_OK){
    printf("FAILED\nTrying VID=0x0403, PID=0x6014...");
    ft_status = FT_SetVIDPID(0x0403,0x6014);
    if (ft_status != FT_OK){
      fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
      return EXIT_FAILURE;
    }
    ft_status = FT_Open(iport, &ft_handle);
  }
  //Exit program if we still haven't opened the device
  if (ft_status != FT_OK){
    printf("FAILED\n");
    fprintf(stderr,"ERROR : Failed to open device : ft_status = %d\nHave you tried (sudo rmmod ftdi_sio)?\n",ft_status);
    return EXIT_FAILURE;
  }
  printf("SUCCESS\n");
  

  /* Erase the EEPROM to set the device back to UART mode */
  printf("Erasing device EEPROM\n");
  ft_status = FT_EraseEE(ft_handle);
  if(ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Device EEPROM could not be erased : ft_status = %d\n", ft_status);
    return EXIT_FAILURE;
  }

  /* Assign appropriate values to eeprom data */
//	eeprom_data.Signature1 = 0x00000000;
//	eeprom_data.Signature2 = 0xffffffff;
////  eeprom_data.Version = 0; //FT232H
//	eeprom_data.VendorId = 0x0403;				
//	eeprom_data.ProductId = 0x6014;
//	eeprom_data.Manufacturer = "FTDI";
//	eeprom_data.ManufacturerId = "FT";
//	eeprom_data.Description = "Piksi UART over USB";
////	eeprom_data.SerialNumber = NULL;		// if fixed, or NULL

  /* Program device EEPROM */
//  printf("Programming device EEPROM\n");
//	ft_status = FT_EE_Program(ft_handle, &eeprom_data);
//	if(ft_status != FT_OK) {
//    fprintf(stderr,"ERROR : Failed to program device EEPROM : ft_status = %d\n",ft_status);
//    return EXIT_FAILURE;
//	}

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
	return EXIT_SUCCESS;
}
