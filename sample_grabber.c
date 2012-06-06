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
#include <time.h>
#include <ftd2xx.h>

/* Block size to transfer. */
#define XFER_LEN (16*1024)
char rx_buff[XFER_LEN];

/* USB device description, used to find the device to open. */
#define USB_DEVICE_DESC "Piksi Passthrough"
#define USB_CUSTOM_VID 0x0403
#define USB_CUSTOM_PID 0x8398

/* Mapping from raw sign-magnitude format to two's complement.
 * See MAX2769 Datasheet, Table 2. */
char raw_value_mapping[8] = {1, 3, 5, 7, -1, -3, -5, -7};

#define MBS 16

int main()
{
  int verbose = 2;
  long bytes_wanted = MBS*1000*1000;
  char* filename[80] = "tehdataz";

  FT_STATUS ft_status;
  FT_HANDLE ft_handle;

  /* Set our custom USB VID and PID in the driver so the device can be
   * identified. */
  ft_status = FT_SetVIDPID(USB_CUSTOM_VID, USB_CUSTOM_PID);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Setting custom PID failed!"
                    " (status=%d)\n", ft_status);
    return 1;
  }

  /* Open the first device matching with description matching
   * USB_DEVICE_DESC. */
  ft_status = FT_OpenEx(USB_DEVICE_DESC, FT_OPEN_BY_DESCRIPTION, &ft_handle);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Unable to open device!"
                    " (status=%d)\n", ft_status);
    return 1;
  }

  /* Device was opened OK, let's get some more information about it. */
  FT_DEVICE ft_type;
  DWORD id;
  char serial_number[80];
  char desc[80];
  ft_status = FT_GetDeviceInfo(ft_handle, &ft_type, &id, serial_number,
                               desc, NULL);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Could not get device info!"
                    " (status=%d)\n", ft_status);
    FT_Close(ft_handle);
    return 1;
  }

  if (verbose > 0)
    printf("Device opened, serial number: %s\n", serial_number);
  if (verbose > 1) {
    printf("Device Info:\n");
    printf("  Description: %s\n", desc);
    printf("  FTDI Type: %d\n", ft_type);
    printf("  VID: 0x%04X\n", (id >> 16));
    printf("  PID: 0x%04X\n", (id & 0xFFFF));
  }

  /* Set FTDI device into FT245 Synchronous FIFO mode. NOTE: This mode must
   * _also_ be enabled in the device's EEPROM! */
  ft_status = FT_SetBitMode(ft_handle, 0xFF, FT_BITMODE_SYNC_FIFO);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Setting FTDI bit mode failed!"
                    " (status=%d)\n", ft_status);
    FT_Close(ft_handle);
    return 1;
  }

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
    return 1;
  }
  ft_status = FT_SetUSBParameters(ft_handle, 0x10000, 0x10000);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Setting USB transfer size failed!"
                    " (status=%d)\n", ft_status);
    FT_Close(ft_handle);
    return 1;
  }
  ft_status = FT_SetFlowControl(ft_handle, FT_FLOW_RTS_CTS, 0, 0);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Setting flow control mode failed!"
                    " (status=%d)\n", ft_status);
    FT_Close(ft_handle);
    return 1;
  }

  /* Purge the receive buffer on the FTDI device of any old data. */
  ft_status = FT_Purge(ft_handle, FT_PURGE_RX);
  if (ft_status != FT_OK) {
    fprintf(stderr, "ERROR: Purging RX buffer failed!"
                    " (status=%d)\n", ft_status);
    FT_Close(ft_handle);
    return 1;
  }

  FILE* fp = fopen(filename, "w");
  if (ferror(fp)) {
    perror("Error opening output file");
    FT_Close(ft_handle);
    fclose(fp);
    return 1;
  }

  DWORD n_rx;
  long total_n_rx = 0;

  time_t t0, t1;

  /* To make sure the internal SwiftNAP buffers are flushed we just discard a
   * bit of data at the beginning. */
  for (int i=0; i<8; i++) {
    ft_status = FT_Read(ft_handle, rx_buff, XFER_LEN, &n_rx);
    if (ft_status != FT_OK) {
      fprintf(stderr, "ERROR: Purge read from device failed!"
                      " (status=%d)\n", ft_status);
      FT_Close(ft_handle);
      fclose(fp);
      return 1;
    }
  }

  /* Store transfer starting time for later. */
  t0 = time(NULL);

  /* Capture data! */
  while (total_n_rx < bytes_wanted) {
    /* Ask for XFER_LEN bytes from the device. */
    ft_status = FT_Read(ft_handle, rx_buff, XFER_LEN, &n_rx);
    if (ft_status != FT_OK) {
      fprintf(stderr, "ERROR: Read from device failed!"
                      " (status=%d)\n", ft_status);
      FT_Close(ft_handle);
      fclose(fp);
      return 1;
    }

    /* Ok, we received n_rx bytes. */
    total_n_rx += n_rx;

    /* Write the data into the file. */
    for (int i=0; i<XFER_LEN; i++) {

      /* Raw byte format:
       *
       *   [7-5] - Sample 0
       *   [4-2] - Sample 1
       *   [1-0] - Flags, reserved for future use.
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
       * easier post-processing.
       */

      /* Write sample 0. */
      char sample0 = raw_value_mapping[(rx_buff[i]>>5) & 0x7];
      fputc(sample0, fp);

      /* Write sample 1. */
      /* TODO: When the v2.3 hardware is ready fix the interleaving here. */
      fputc(0, fp);
      /*fputc(raw_value_mapping[(rx_buff[i]>>2) & 0x7], fp);*/

      if (ferror(fp)) {
        perror("Error writing to output file");
        FT_Close(ft_handle);
        fclose(fp);
        return 1;
      }
    }
  }

  /* Store transfer finishing time. */
  t1 = time(NULL);
  double t = t1 - t0;

  /* Clean up. */
  FT_Close(ft_handle);
  fclose(fp);

  /* Print some statistics. */
  if (verbose > 0) {
    printf("Done!\n");
    printf("%.2f MB in %.2f seconds, %.3f MB/s\n",
           bytes_wanted / 1e6,
           t,
           (bytes_wanted / 1e6) / t
    );
  }

  return 0;
}

