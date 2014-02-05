/*
 * Copyright (C) 2012-2014 Swift Navigation Inc.
 *
 * Contact: Fergus Noble <fergus@swift-nav.com>
 *          Colin Beighley <colin@swift-nav.com>
 *
 * This source is subject to the license found in the file 'LICENSE' which must
 * be be distributed together with this source. All other rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 *   "set_uart_mode.c"
 *
 *   Purpose : Erases EEPROM attached to FT232H to set the FT232H in UART mode
 *             for normal operation. Should be used after running
 *             sample_grabber.
 *
 *   Usage :   Plug in device and run set_uart_mode.
 *
 *   Options : ./set_uart_mode [-v] [-i] [-h]
 *             [--verbose -v]  Print more verbose output.
 *             [--id -i]       Product ID of Piksi to set into UART MODE.
 *                               Default is 0x8398.
 *                               Valid range 0x0001 to 0xFFFF.
 *             [--help -h]     Print this information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "ftd2xx.h"

/* FTDI VID / Piksi custom PID. */
#define USB_CUSTOM_VID 0x0403
#define USB_CUSTOM_PID 0x8398

FT_HANDLE ft_handle;
FT_STATUS ft_status;
int verbose = 0;

int pid = USB_CUSTOM_PID;

void print_usage()
{
  printf("Usage: set_uart_mode [-v] [-i pid] [-h]\n"
         "Options:\n"
         "  [--verbose -v]  Print more verbose output.\n"
         "  [--id -i]       Product ID of Piksi to set into UART MODE.\n"
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

  /* Find out if it is hex or decimal. Hex preceeded by '0x'. */
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

  int iport = 0;

  /* Try to open device using input PID, or our custom PID if no input. */
  if (verbose)
    printf("Trying to open with VID=0x%04x, PID=0x%04x...",USB_CUSTOM_VID,pid);
  ft_status = FT_SetVIDPID(USB_CUSTOM_VID, pid);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  ft_status = FT_Open(iport, &ft_handle);

  /* Exit program if we haven't opened the device. */
  if (ft_status != FT_OK){
    if (verbose)
      printf("FAILED\n");
    fprintf(stderr,"ERROR : Failed to open device : ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  if (verbose)
    printf("SUCCESS\n");

  /* Erase the EEPROM to set the device to UART mode. */
  if (verbose)
    printf("Erasing device EEPROM\n");
  ft_status = FT_EraseEE(ft_handle);
  if(ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Device EEPROM could not be erased : ft_status = %d\n", ft_status);
    return EXIT_FAILURE;
  }

  /* Reset the device. */
  if (verbose)
    printf("Resetting device\n");
  ft_status = FT_ResetDevice(ft_handle);
  if(ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Device could not be reset : ft_status = %d\n", ft_status);
    return EXIT_FAILURE;
  }

  /* Close the device. */
  if (verbose)
    printf("Closing device\n");
  FT_Close(ft_handle);
  if(ft_status != FT_OK) {
    fprintf(stderr,"ERROR : Failed to close device : ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }

  if (verbose)
    printf("Re-configuring for UART mode successful, please unplug and replug your device now\n");
  return EXIT_SUCCESS;
}
