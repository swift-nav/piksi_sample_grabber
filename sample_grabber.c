/*
 * Copyright (C) 2012 Swift Navigation Inc.
 * Contact: Fergus Noble <fergus@swift-nav.com>
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

/* Block size to transfer. */
#define XFER_LEN (16*1024)
char rx_buff[XFER_LEN];

/* Piksi Passthrough USB device description, used to find the device to open. */
#define USB_PASSTHRU_DESC "Piksi Passthrough"
#define USB_CUSTOM_VID 0x0403
#define USB_CUSTOM_PID 0x8398

/* FPGA FIFO has overflowed when bit 0 of the transferred byte is 1 */
#define FPGA_FIFO_OVERFLOW 0x01

/* Mapping from raw sign-magnitude format to two's complement.
 * See MAX2769 Datasheet, Table 2. */
char sign_mag_mapping[8] = {1, 3, 5, 7, -1, -3, -5, -7};
char no_mapping[8] = {0, 1, 2, 3, 4, 5, 6, 7};

/* Global state so it can be accessed from the exit handler. */
FILE* fp;
FT_HANDLE ft_handle;
int verbose = 0;
long total_n_rx = 0;
time_t t0;

void exit_handler();

void print_usage()
{
  printf("Usage: sample_grabber [options] file\n"
         "Options:\n"
         "  [-N]             Disable raw value mapping.\n"
         "                   This disables the mapping of values from raw\n"
         "                   front-end output to two's complement values.\n"
         "  [--help -h]      Print this information.\n"
         "  [--verbose -v]   Print more verbose output.\n"
         "  [--size -s SIZE] Stop transfer after SIZE samples have been\n"
         "                   transferred. Suffixes 'k' and 'M' are permitted\n"
         "                   to multiply by 1e3 and 1e6 respectively.\n"
  );
}

/** Parse a string representing a number of samples to an integer.  String can
 * be a plain number or can include a unit suffix. This can be one of 'k' or
 * 'M' which multiply by 1e3 and 1e6 respectively.
 *
 * e.g. "5" -> 5
 *      "2k" -> 2000
 *      "3M" -> 3000000
 *
 * Returns -1 on an error condition.
 */
long int parse_size(char * s)
{
  long int val = -1;
  char last = s[strlen(s)-1];

  /* If the last character is a digit then just return the string as a
   * number.
   */
  if (isdigit(last)) {
    val = atol(s);
    if (val != 0)
      return val;
    else
      return -1;
  }

  /* Last char is a unit suffix, find the numeric part of the value. */
  /* Delete the suffix. */
  s[strlen(s)-1] = 0;
  /* Convert to a long int. */
  val = atol(s);
  if (val == 0)
    return -1;

  /* Multiply according to suffix and return value. */
  switch (last) {
    case 'k':
    case 'K':
      return val*1e3;
      break;

    case 'M':
      return val*1e6;
      break;

    default:
      return -1;
  }
}

int main(int argc, char *argv[])
{
  int disable_mapping = 0;
  long int bytes_wanted = 16*1000*1000;

  static const struct option long_opts[] = {
    {"no-mapping", no_argument,       NULL, 'N'},
    {"size",       required_argument, NULL, 's'},
    {"verbose",    no_argument,       NULL, 'v'},
    {"help",       no_argument,       NULL, 'h'},
    {NULL,         no_argument,       NULL, 0}
  };

  opterr = 0;
  int c;
  int option_index = 0;
  while ((c = getopt_long(argc, argv, "Ns:vh", long_opts, &option_index)) != -1)
    switch (c) {
      case 'N':
        disable_mapping = 1;
        break;
      case 'v':
        verbose++;
        break;
      case 's': {
        long int samples_wanted = parse_size(optarg);
        if (samples_wanted < 0) {
          fprintf(stderr, "Invalid size argument.\n");
          return EXIT_FAILURE;
        }
        /* 2 samples per byte. */
        bytes_wanted = samples_wanted / 2;
        break;
      }
      case 'h':
        print_usage();
        return EXIT_SUCCESS;
      case '?':
        if (optopt == 's')
          fprintf(stderr, "Transfer size option requires an argument.\n");
        else
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        return EXIT_FAILURE;
      default:
        abort();
     }

  /* Output filename is the first non-option argument. */
  if (optind >= argc) {
    fprintf(stderr, "Please specify an output filename.\n");
    return EXIT_FAILURE;
  }
  char* filename = argv[optind];

  char* mapping;
  if (disable_mapping) {
    if (verbose > 0)
      printf("Raw value mapping is disabled.\n");
    mapping = no_mapping;
  } else {
    mapping = sign_mag_mapping;
  }

  if (verbose > 0)
    printf("Transfering %lu bytes (%lu samples).\n", bytes_wanted, bytes_wanted*2);

  FT_STATUS ft_status;
  DWORD VID, PID;
  int iport = 0;

  //Get VID/PID from FTDI device
  if (verbose > 0)
    printf("Getting VID/PID from device\n");
  ft_status = FT_GetVIDPID(&VID,&PID);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to get VID and PID from FTDI device, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }
  if (verbose > 0)
    printf("    VID = %04x, PID = %04x\n",VID,PID);
  //Set the VID/PID found
  if (verbose > 0)
    printf("Setting VID/PID\n");
  ft_status = FT_SetVIDPID(VID,PID);
  if (ft_status != FT_OK){
    fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
    return EXIT_FAILURE;
  }

  //TODO : find a better way to do this
  //If VID/PID is already 0403/8398, we assume we have already programmed the EEPROM
  //If not, the device must be unplugged and replugged after the EEPROM is erased
  //and programmed
  int gotta_replug = 1;

  //Open the device
  if (verbose > 0)
    printf("Attempting to open device using read VID/PID...");
  ft_status = FT_Open(iport, &ft_handle);
  //If that didn't work, try some other likely VID/PID combos
  //Try 0403:6014
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
  //Try 0403:8398
  if (ft_status != FT_OK){
    if (verbose > 0)
      printf("FAILED\nTrying VID=0x0403, PID=0x8398...");
    ft_status = FT_SetVIDPID(0x0403,0x8398);
    if (ft_status != FT_OK){
      fprintf(stderr,"ERROR : Failed to set VID and PID, ft_status = %d\n",ft_status);
      return EXIT_FAILURE;
    }
    ft_status = FT_Open(iport, &ft_handle);
    if (ft_status == FT_OK)
      gotta_replug = 0;
  }
  //Exit program if we still haven't opened the device
  if (ft_status != FT_OK){
    if (verbose > 0)
      printf("FAILED\n");
    fprintf(stderr,"ERROR : Failed to open device : ft_status = %d\nHave you tried (sudo rmmod ftdi_sio)?\n",ft_status);
    return EXIT_FAILURE;
  }
  if (verbose > 0)
    printf("SUCCESS\n");

  //Device needs to be programmed in FIFO mode and unplugged/replugged
  if (gotta_replug == 1){
    /* Erase the EEPROM */
    ft_status = FT_EraseEE(ft_handle);
    if(ft_status != FT_OK) {
      fprintf(stderr, "ERROR: Device EEPROM could not be erased : ft_status = %d\n", ft_status);
      return EXIT_FAILURE;
    }
    if (verbose > 0)
      printf("Erased device's EEPROM\n");

    /* Assign appropriate values to eeprom data */
    FT_PROGRAM_DATA eeprom_data;

    eeprom_data.Signature1 = 0x00000000;
    eeprom_data.Signature2 = 0xffffffff;
    eeprom_data.Version = 5; //5=FT232H
    eeprom_data.VendorId = USB_CUSTOM_VID;        
    eeprom_data.ProductId = USB_CUSTOM_PID;
    eeprom_data.Manufacturer = "FTDI";
    eeprom_data.ManufacturerId = "FT";
    eeprom_data.Description = "Piksi Passthrough";
    eeprom_data.IsFifoH = 1; //needed for FIFO samples passthrough

    /* Program device EEPROM */
    ft_status = FT_EE_Program(ft_handle, &eeprom_data);
    if(ft_status != FT_OK) {
      fprintf(stderr,"ERROR : Failed to program device EEPROM : ft_status = %d\n",ft_status);
      return EXIT_FAILURE;
    }
    if (verbose > 0)
      printf("Programmed device's EEPROM\n");

    /* Reset the device */
    ft_status = FT_ResetDevice(ft_handle);
    if(ft_status != FT_OK) {
      fprintf(stderr, "ERROR: Device could not be reset : ft_status = %d\n", ft_status);
      return EXIT_FAILURE;
    }
    if (verbose > 0)
      printf("Reset device\n");
    
    printf("Please unplug and replug your device and run sample_grabber again\n");
    return EXIT_SUCCESS;
  }

  /* Get some more information about the device. */ 
//  FT_DEVICE ft_type;
//  DWORD id;
//  char serial_number[80];
//  char desc[80];
//  ft_status = FT_GetDeviceInfo(ft_handle, &ft_type, &id, serial_number,
//                               desc, NULL);
//  if (ft_status != FT_OK) {
//    fprintf(stderr, "ERROR: Could not get device info!"
//                    " (status=%d)\n", ft_status);
//    FT_Close(ft_handle);
//    return EXIT_FAILURE;
//  }
//
//  if (verbose > 0)
//    printf("Device opened, serial number: %s\n", serial_number);
//  if (verbose > 1) {
//    printf("Device Info:\n");
//    printf("  Description: %s\n", desc);
//    printf("  FTDI Type: %d\n", ft_type);
//    printf("  VID: 0x%04X\n", (id >> 16));
//    printf("  PID: 0x%04X\n", (id & 0xFFFF));
//  }

  /* Set FTDI device into FT245 Synchronous FIFO mode. NOTE: This mode must
   * _also_ be enabled in the device's EEPROM! */
  ft_status = FT_SetBitMode(ft_handle, 0xFF, FT_BITMODE_SYNC_FIFO);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Setting FTDI bit mode failed!"
                    " (status=%d)\n", ft_status);
    FT_Close(ft_handle);
    return EXIT_FAILURE;
  }
  if (verbose > 0)
    printf("Set device in FT245 Synchronous FIFO mode\n");
  
  /* Configure FTDI device and driver for maximum performance. Some of these
   * configuration values are given in the following FTDI documents but they
   * still remain a bit of a mystery:
   *
   *   - AN130: "FT2232H Used In An FT245 Style Synchronous FIFO Mode"
   *
   *   - AN165: "Establishing Synchronous 245 FIFO Communications using a
   *             Morph-IC-II"
   */
  ft_status = FT_SetLatencyTimer(ft_handle, 2);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Setting Latency Timer failed!"
                    " (status=%d)\n", ft_status);
    FT_Close(ft_handle);
    return EXIT_FAILURE;
  }
  if (verbose > 0)
  ft_status = FT_SetUSBParameters(ft_handle, 0x10000, 0x10000);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Setting USB transfer size failed!"
                    " (status=%d)\n", ft_status);
    FT_Close(ft_handle);
    return EXIT_FAILURE;
  }
  ft_status = FT_SetFlowControl(ft_handle, FT_FLOW_RTS_CTS, 0, 0);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Setting flow control mode failed!"
                    " (status=%d)\n", ft_status);
    FT_Close(ft_handle);
    return EXIT_FAILURE;
  }
  if (verbose > 0)
    printf("Set driver parameters for optimal performance\n");

  /* Purge the receive buffer on the FTDI device of any old data. */
  ft_status = FT_Purge(ft_handle, FT_PURGE_RX);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Purging RX buffer failed!"
                    " (status=%d)\n", ft_status);
    FT_Close(ft_handle);
    return EXIT_FAILURE;
  }
  if (verbose > 0)
    printf("Purged FIFO buffer on FTDI chip onboard device\n");

  fp = fopen(filename, "w");
  if (ferror(fp)) {
    perror("Error opening output file");
    FT_Close(ft_handle);
    fclose(fp);
    return EXIT_FAILURE;
  }
  if (verbose > 0)
    printf("Opened file ""%s"" for writing samples to\n",filename);

  DWORD n_rx;
  
  /* To make sure the internal SwiftNAP buffers are flushed we just discard a
   * bit of data at the beginning. */
  for (int i=0; i<8; i++) {
    ft_status = FT_Read(ft_handle, rx_buff, XFER_LEN, &n_rx);
    if (ft_status != FT_OK) {
      fprintf(stderr, "ERROR: Purge read from device failed!"
                      " (status=%d)\n", ft_status);
      FT_Close(ft_handle);
      fclose(fp);
      return EXIT_FAILURE;
    }
  }
  if (verbose > 0)
    printf("Read a good number of samples to allow SwiftNAP buffers time to flush\n");

  /* Store transfer starting time for later. */
  t0 = time(NULL);

  /* Register our SIGINT handler so we can let the user cleanly stop the
   * transfer with Ctrl-C. */
  signal(SIGINT, exit_handler);

  /* Capture data! */
  int break_flag = 0;
  while (total_n_rx < bytes_wanted) {

    /* See if we've broken out of the nested mapping for loop */
    if (break_flag) break;

    /* Ask for XFER_LEN bytes from the device. */
    ft_status = FT_Read(ft_handle, rx_buff, XFER_LEN, &n_rx);
    if (ft_status != FT_OK) {
      fprintf(stderr, "ERROR: Read from device failed!"
                      " (status=%d)\n", ft_status);
      FT_Close(ft_handle);
      fclose(fp);
      return EXIT_FAILURE;
    }

    /* Ok, we received n_rx bytes. */
    total_n_rx += n_rx;

    /* Write the data into the file. */
    for (int i=0; i<XFER_LEN; i++) {

      /* Raw byte format:
       *
       *   [7-5] - Sample 0
       *   [4-2] - Sample 1
       *   [1] - Flag, reserved for future use.
       *   [0] - FIFO Overflow Flag, FPGA's internal FIFO has overflowed
       *
       * We check the FIFO Overflow Flag in each of the sample sets
       * to make sure that we haven't dropped any samples. If we do we stop
       * recording samples as there will be a discontinuity we we continue
       * recording.
       *
       * Samples are packed as follows:
       *
       *   [MAX2769_I1, MAX2769_I0, MAX2769_Q1]
       *
       * The Piksi firmware by default configures the MAX2769 such that this
       * represents a 3-bit real sample in sign-magnitude format.
       *
       * Before writing the sample to the output file we use a lookup table to
       * convert the sample into a signed byte in two's-complement form for
       * easier post-processing (unless mapping is disabled).
       */
      
      /* Check the FIFO Overflow Flag */
      if (rx_buff[i] & FPGA_FIFO_OVERFLOW) {
        printf("FPGA FIFO overflow detected, stopping here and exiting\n");
        break_flag = 1;
        break;
      }

      /* Write sample 0. */
      char sample0 = mapping[(rx_buff[i]>>5) & 0x7];
      fputc(sample0, fp);

      /* Write sample 1. */
      char sample1 = mapping[(rx_buff[i]>>2) & 0x7];
      /* TODO: When the v2.3 hardware is ready fix the interleaving here. */
      fputc(sample1, fp);
      /*fputc(0, fp);*/

      if (ferror(fp)) {
        perror("Error writing to output file");
        FT_Close(ft_handle);
        fclose(fp);
        return EXIT_FAILURE;
      }
    }
  }

  if (verbose > 0)
    printf("Finished receiving samples\n");
  exit_handler(0);
  return EXIT_SUCCESS;
}

void exit_handler(int sig)
{
  /* Ignore SIGINT inside the handler. */
  if (sig)
    signal(SIGINT, SIG_IGN);

  /* Transfer finishing time. */
  time_t t1 = time(NULL);
  double t = t1 - t0;

  /* Clean up. */
  FT_Close(ft_handle);
  fclose(fp);

  /* Print some statistics. */
  printf("%.2f MSamples in %.2f seconds, %.3f MS/s\n",
         2*total_n_rx / 1e6,
         t,
         2*(total_n_rx / 1e6) / t
  );
  exit(0);
}
