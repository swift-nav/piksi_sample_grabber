/*
 * Copyright (C) 2012-2013 Swift Navigation Inc.
 *
 * Contacts: Fergus Noble <fergus@swift-nav.com>
 *           Colin Beighley <colin@swift-nav.com>
 *
 * This source is subject to the license found in the file 'LICENSE' which must
 * be be distributed together with this source. All other rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 *   "set_fifo_mode.c"
 *
 *   Purpose : Writes settings to EEPROM attached to FT232H for synchronous
 *             FIFO mode in order to stream raw samples from the RF frontend
 *             through the FPGA. Must be used before running sample_grabber.
 *
 *   Usage :   Plug in device and run set_fifo_mode. You may have to use
 *             sudo rmmod ftdi_sio first. 
 *
 *   Options : ./set_fifo_mode [-p] [-v] [-h]
 *             [--prompt -p]    Don't prompt to confirm that device is correct.
 *             [--verbose -v]   Print more verbose output.
 *             [--help -h]      Print this information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "ftd2xx.h"

/* Piksi custom FTDI PID */
#define USB_CUSTOM_VID 0x0403
#define USB_CUSTOM_PID 0x8398

FT_HANDLE ft_handle;
FT_STATUS ft_status;
int verbose = 0;
int dont_prompt = 0;

FT_PROGRAM_DATA eeprom_data;

void print_usage()
{
  printf("Usage: set_fifo_mode [-p] [-v] [-h]\n"
         "Options:\n"
         "  [--prompt -p]    Don't prompt to confirm that device is correct.\n"
         "  [--verbose -v]   Print more verbose output.\n"
         "  [--help -h]      Print this information.\n"
  );
}

int main(int argc, char *argv[])
{

  static const struct option long_opts[] = {
    {"prompt",     no_argument,       NULL, 'p'},
    {"verbose",    no_argument,       NULL, 'v'},
    {"help",       no_argument,       NULL, 'h'},
    {NULL,         no_argument,       NULL, 0}
  };

  opterr = 0;
  int c;
  int option_index = 0;
  while ((c = getopt_long(argc, argv, "vph", long_opts, &option_index)) != -1)
    switch (c) {
      case 'v':
        verbose++;
        break;
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
  
  DWORD num_devs;
  DWORD VID, PID;
  int iport = 0;

  /* See how many devices are plugged in, fail if greater than 1 */
  if (verbose)
    printf("Creating device info list\n");
  ft_status = FT_CreateDeviceInfoList(&num_devs);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to create device info list, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  if (verbose)
    printf("Making sure only one FTDI device is plugged in\n");
  /* Check that there aren't more than 1 device plugged in */
  if (num_devs > 1){
    fprintf(stderr,"ERROR : More than one FTDI device plugged in\n");
    return EXIT_FAILURE;
  }

  /* Get VID/PID from FTDI device */
  if (verbose)
    printf("Getting VID/PID from device\n");
  ft_status = FT_GetVIDPID(&VID,&PID);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to get VID and PID from FTDI device, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  if (verbose)
    printf("    VID = %04x, PID = %04x\n",VID,PID);
  /* Set the VID/PID found */
  if (verbose)
    printf("Setting VID/PID\n");
  ft_status = FT_SetVIDPID(VID,PID);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }

  /* Get info about the device and print to user */
  //FT_GetDeviceInfoDetail seems to not work very reliably
  //DWORD lpdw_flags;
  //DWORD lpdw_type;
  //DWORD lpdw_id;
  //char pc_serial_number[16];
  //char pc_description[64];
  //ft_status = FT_GetDeviceInfoDetail(0,&lpdw_flags,&lpdw_type,&lpdw_id,NULL,pc_serial_number,pc_description,&ft_handle);
  //if (ft_status != FT_OK){
  //  fprintf(stderr,"ERROR : Failed to get device info, ft_status = %d\n",ft_status);
  //  return EXIT_FAILURE;
  //}
  //if (verbose || !dont_prompt){
  //  printf("Device Information : \n");
  //  printf("     Description   : %s\n", pc_description);
  //  printf("     Serial Number : %s\n", pc_serial_number);
  //  printf("     Flags         : 0x%x\n", lpdw_flags);
  //  printf("     Type          : 0x%x\n", lpdw_type);
  //  printf("     ID            : 0x%x\n", lpdw_id);
  //}

  /* Ask user if this is the correct device */
  if (!dont_prompt) {
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

  /* Open the device */
  if (verbose)
    printf("Attempting to open device using read VID/PID...");
  ft_status = FT_Open(iport, &ft_handle);
  /* If that didn't work, try some other likely VID/PID combos */
  /* Try 0403:6014 */
  if (ft_status != FT_OK){
    if (verbose)
      printf("FAILED\nTrying VID=0x0403, PID=0x6014...");
    ft_status = FT_SetVIDPID(0x0403,0x6014);
    if (ft_status != FT_OK){
      fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
      return EXIT_FAILURE;
    }
    ft_status = FT_Open(iport, &ft_handle);
  }
  /* Try 0403:8398 */
  if (ft_status != FT_OK){
    if (verbose)
      printf("FAILED\nTrying VID=0x0403, PID=0x8398...");
    ft_status = FT_SetVIDPID(0x0403,0x8398);
    if (ft_status != FT_OK){
      fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
      return EXIT_FAILURE;
    }
    ft_status = FT_Open(iport, &ft_handle);
  }
  /* Exit program if we still haven't opened the device */
  if (ft_status != FT_OK){
    if (verbose)
      printf("FAILED\n");
    fprintf(stderr,"ERROR : Failed to open device : ft_status = %d\nHave you tried (sudo rmmod ftdi_sio)?\n",ft_status);
    return EXIT_FAILURE;
  }
  if (verbose)
    printf("SUCCESS\n");

  /* Erase the EEPROM */
  ft_status = FT_EraseEE(ft_handle);
  if(ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Device EEPROM could not be erased : ft_status = %d\n", ft_status);
    return EXIT_FAILURE;
  }
  if (verbose)
    printf("Erased device's EEPROM\n");

  /* Assign appropriate values to eeprom data */
  eeprom_data.Signature1 = 0x00000000;
  eeprom_data.Signature2 = 0xffffffff;
  eeprom_data.Version = 5; /* 5=FT232H */
  eeprom_data.VendorId = USB_CUSTOM_VID;        
  eeprom_data.ProductId = USB_CUSTOM_PID;
  eeprom_data.Manufacturer = "FTDI";
  eeprom_data.ManufacturerId = "FT";
  eeprom_data.Description = "Piksi Passthrough";
  eeprom_data.IsFifoH = 1; /* needed for FIFO samples passthrough */

  /* Program device EEPROM */
  ft_status = FT_EE_Program(ft_handle, &eeprom_data);
  if (ft_status != FT_OK) {
    fprintf(stderr,"ERROR : Failed to program device EEPROM : ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  if (verbose)
    printf("Programmed device's EEPROM\n");

  /* Reset the device */
  ft_status = FT_ResetDevice(ft_handle);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Device could not be reset : ft_status = %d\n", ft_status);
    return EXIT_FAILURE;
  }
  if (verbose)
    printf("Reset device\n");

  /* Close the device */
  if (verbose)
    printf("Closing device\n");
  FT_Close(ft_handle);
  if(ft_status != FT_OK) {
    fprintf(stderr,"ERROR : Failed to close device : ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }

  printf("Re-configuring for FIFO mode successful, please unplug and replug your device now\n");

  return EXIT_SUCCESS;
}
