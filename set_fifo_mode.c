/*
 * Copyright (C) 2012-2014 Swift Navigation Inc.
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
 *   Options : ./set_fifo_mode [-v] [-i] [-h]
 *             [--verbose -v]  Print more verbose output.
 *             [--id -i]       Product ID to assign to Piksi.
 *                               Default is 0x8398.
 *                               Valid range 0x0001 to 0xFFFF.
 *             [--help -h]     Print this information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "ftd2xx.h"
#include "libusb_hacks.h"

/* FTDI VID / Piksi custom PID. */
#define USB_CUSTOM_VID 0x0403
#define USB_CUSTOM_PID 0x8398

#define USB_DEFAULT_VID 0x0403
#define USB_DEFAULT_PID 0x6014

FT_HANDLE ft_handle;
FT_STATUS ft_status;
int verbose = 0;

int pid = USB_CUSTOM_PID;

FT_PROGRAM_DATA eeprom_data;

void print_usage()
{
  printf("Usage: set_fifo_mode [-v] [-i pid] [-h]\n"
         "Options:\n"
         "  [--verbose -v]  Print more verbose output.\n"
         "  [--id -i]       Product ID to assign to Piksi.\n"
         "                    Default is 0x8398.\n"
         "                    Valid range 0x0001 to 0xFFFF.\n"
         "  [--help -h]     Print this information.\n"
  );
}

/* Parse command line arg --id, USB Product ID.
 * Input: PID in hex or decimal format, i.e. '0xFFFF' or '65535'.
 *
 * Returns 0x0001 to 0xFFFF as valid ID, or 0 for error.
 */
int parse_pid(char *arg) {
  int pid = 0;

  /* Find out if it is hex or base 10. Hex preceeded by '0x'. */
  if ((arg[0] == '0') && (arg[1] == 'x')) {
    /* Hexadecimal. */
    /* Check if length is <= 6, i.e. '0xFFFF'. */
    if (strlen(arg) > 6)
      return 0;
    pid = (int)strtol(arg+2, NULL, 16);
  } else {
    /* Decimal. */
    /* Check if length is <= 5, i.e. '65535'. */
    if (strlen(arg) > 5)
      return 0;
    pid = atoi(arg);
  }

  return pid;
}

int main(int argc, char *argv[])
{

  static const struct option long_opts[] = {
    {"verbose",  no_argument,        NULL, 'v'},
    {"help",     no_argument,        NULL, 'h'},
    {"id",       required_argument,  NULL, 'i'},
    {NULL,       no_argument,        NULL, 0}
  };

  opterr = 0;
  int c;
  int option_index = 0;
  while ((c = getopt_long(argc, argv, "vhi:", long_opts, &option_index)) != -1)
    switch (c) {
      case 'v':
        verbose++;
        break;
      case 'h':
        print_usage();
        return EXIT_SUCCESS;
      case 'i': {
        pid = parse_pid(optarg);
        if (!pid) {
          fprintf(stderr, "Invalid ID argument.\n");
          return EXIT_FAILURE;
        }
        break;
      }
      case '?':
        if (optopt == 'i')
          fprintf(stderr, "ID argument requires an argument.\n");
        else
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        return EXIT_FAILURE;
      default:
        abort();
     }

  DWORD num_devs;
  int iport = 0;

  /* See how many devices are plugged in, fail if greater than 1. */
  if (verbose)
    printf("Creating device info list\n");
  ft_status = FT_CreateDeviceInfoList(&num_devs);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to create device info list, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  /* Check that there aren't more than 1 device plugged in. */
  if (verbose)
    printf("Making sure only one FTDI device is plugged in\n");
  if (num_devs > 1){
    fprintf(stderr,"ERROR : More than one FTDI device plugged in\n");
    return EXIT_FAILURE;
  }

  /* Try to open device using default FTDI VID/PID. */
  if (verbose)
    printf("Trying to open with VID=0x%04x, PID=0x%04x...", USB_DEFAULT_VID,
                                                            USB_DEFAULT_PID);
  ft_status = FT_SetVIDPID(USB_DEFAULT_VID, USB_DEFAULT_PID);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }

  #ifdef __linux
    usb_detach_kernel_driver(USB_DEFAULT_VID, USB_DEFAULT_PID);
  #endif

  ft_status = FT_Open(iport, &ft_handle);
  /* Exit program if we haven't opened the device. */
  if (ft_status != FT_OK){
    if (verbose)
      printf("FAILED\n");
    fprintf(stderr,"ERROR : Failed to open device : ft_status = %d\n", ft_status);
    if (ft_status == FT_DEVICE_NOT_OPENED)
      #ifdef __linux
        fprintf(stderr,
        "Linux users: enter the following command " \
        "and then run set_fifo_mode again:\n" \
        "    sudo rmmod ftdi_sio\n");
      #elif __APPLE__
        fprintf(stderr,
        "OSX users: enter the following command " \
        "and then run set_fifo_mode again:\n" \
        "    sudo kextunload -b com.FTDI.driver.FTDIUSBSerialDriver\n");
      #endif
    return EXIT_FAILURE;
  }
  if (verbose)
    printf("SUCCESS\n");

  /* Erase the EEPROM. */
  ft_status = FT_EraseEE(ft_handle);
  if(ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Device EEPROM could not be erased : ft_status = %d\n", ft_status);
    return EXIT_FAILURE;
  }
  if (verbose)
    printf("Erased device's EEPROM\n");

  /* Assign appropriate values to eeprom data. */
  eeprom_data.Signature1 = 0x00000000;
  eeprom_data.Signature2 = 0xffffffff;
  eeprom_data.Version = 5; /* 5=FT232H. */
  eeprom_data.VendorId = USB_CUSTOM_VID;
  eeprom_data.ProductId = pid;
  eeprom_data.Manufacturer = "FTDI";
  eeprom_data.ManufacturerId = "FT";
  eeprom_data.Description = "Piksi Passthrough";
  eeprom_data.IsFifoH = 1; /* Needed for FIFO samples passthrough. */

  /* Program device EEPROM. */
  ft_status = FT_EE_Program(ft_handle, &eeprom_data);
  if (ft_status != FT_OK) {
    fprintf(stderr,"ERROR : Failed to program device EEPROM : ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  if (verbose)
    printf("Programmed device's EEPROM, set PID to: 0x%04x\n", pid);

  /* Reset the device. */
  ft_status = FT_ResetDevice(ft_handle);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Device could not be reset : ft_status = %d\n", ft_status);
    return EXIT_FAILURE;
  }
  if (verbose)
    printf("Reset device\n");

  /* Close the device. */
  if (verbose)
    printf("Closing device\n");
  FT_Close(ft_handle);
  if(ft_status != FT_OK) {
    fprintf(stderr,"ERROR : Failed to close device : ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }

  if (verbose)
    printf("Re-configuring for FIFO mode successful, please unplug and replug your device now.\n");

  usb_reset_device(USB_DEFAULT_VID, USB_DEFAULT_PID);
  return EXIT_SUCCESS;
}
