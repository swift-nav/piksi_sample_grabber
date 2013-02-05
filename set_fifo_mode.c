/*
 * Copyright (C) 2012-2013 Swift Navigation Inc.
 * Contacts: Fergus Noble <fergus@swift-nav.com>
 *           Colin Beighley <colin@swift-nav.com>
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
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <getopt.h>
#include <signal.h>

#include "ftd2xx.h"

/* Piksi custom FTDI PID */
#define USB_CUSTOM_VID 0x0403
#define USB_CUSTOM_PID 0x8398

/* Global state so it can be accessed from the exit handler. */
FT_HANDLE ft_handle;
FT_STATUS ft_status;
int verbose = 1;

FT_PROGRAM_DATA eeprom_data;

void print_usage()
{
  printf("Usage: set_fifo_mode [options] file\n"
         "Options:\n"
         "  [--verbose -v]   Print more verbose output.\n"
         "  [--help -h]      Print this information.\n"
  );
}

int main(int argc, char *argv[])
{

  static const struct option long_opts[] = {
    {"verbose",    no_argument,       NULL, 'v'},
    {"help",       no_argument,       NULL, 'h'},
    {NULL,         no_argument,       NULL, 0}
  };

  opterr = 0;
  int c;
  int option_index = 0;
  while ((c = getopt_long(argc, argv, "vh", long_opts, &option_index)) != -1)
    switch (c) {
      case 'v':
        verbose++;
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
  
  DWORD VID, PID;
  int iport = 0;

  /* Get VID/PID from FTDI device */
  if (verbose > 0)
    printf("Getting VID/PID from device\n");
  ft_status = FT_GetVIDPID(&VID,&PID);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to get VID and PID from FTDI device, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  if (verbose > 0)
    printf("    VID = %04x, PID = %04x\n",VID,PID);
  /* Set the VID/PID found */
  if (verbose > 0)
    printf("Setting VID/PID\n");
  ft_status = FT_SetVIDPID(VID,PID);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }

  /* Open the device */
  if (verbose > 0)
    printf("Attempting to open device using read VID/PID...");
  ft_status = FT_Open(iport, &ft_handle);
  /* If that didn't work, try some other likely VID/PID combos */
  /* Try 0403:6014 */
  if (ft_status != FT_OK){
    if (verbose > 0)
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
    if (verbose > 0)
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
    if (verbose > 0)
      printf("FAILED\n");
    fprintf(stderr,"ERROR : Failed to open device : ft_status = %d\nHave you tried (sudo rmmod ftdi_sio)?\n",ft_status);
    return EXIT_FAILURE;
  }
  if (verbose > 0)
    printf("SUCCESS\n");

  /* Device needs to be programmed in FIFO mode and unplugged/replugged */
  /* Erase the EEPROM */
  ft_status = FT_EraseEE(ft_handle);
  if(ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Device EEPROM could not be erased : ft_status = %d\n", ft_status);
    return EXIT_FAILURE;
  }
  if (verbose > 0)
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
  if (verbose > 0)
    printf("Programmed device's EEPROM\n");

  /* Reset the device */
  ft_status = FT_ResetDevice(ft_handle);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Device could not be reset : ft_status = %d\n", ft_status);
    return EXIT_FAILURE;
  }
  if (verbose > 0)
    printf("Reset device\n");

  printf("Please unplug and replug your device\n");

  return EXIT_SUCCESS;
}
