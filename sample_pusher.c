/*
 * Copyright (C) 2009 Micah Dowty
 * Copyright (C) 2010 Uwe Bonnes
 * Copyright (C) 2013 Swift Navigation Inc.
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
 *   "sample_pusher.c"
 *
 *   Purpose : Allows streaming of raw samples from the PC to the FPGA to 
 *             enable repeated firmware runs with the same sample stream.
 *
 *   Usage :   After running set_fifo_mode to set the FT232H on the Piksi to 
 *             fifo mode, use sample_pusher (see arguments below).
 *             After finishing using sample pusher, use set_uart_mode to set 
 *             the FT232H on the Piksi back to UART mode for normal operation.
 *             Note : The STM on the Piksi must use a different UART to 
 *             transmit debug information to the PC than UART6 whilst using 
 *             the sample pusher (you will need to use a separate 3.3 volt 
 *             UART to USB converter with one of the other two picoblade UARTs,
 *             UART1 or UART3).
 *
 *   Options : ./sample_pusher [-s number] [-h] [filename]
 *             [--size -s]     Number of samples to transmit before exiting.
 *                             May be suffixed with a k (1e3) or an M (1e6).
 *                             If no argument is supplied, 
 *             [--help -h]     Print usage information and exit.
 *             [filename]      A filename to save samples to. If none is 
 *                             supplied then samples will not be saved.
 *
 *   Based on the example "stream_test.c" in libftdi, updated in 2010 by Uwe 
 *   Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de> for libftdi and 
 *   originally written by Micah Dowty <micah@navi.cx> in 2009.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "ftdi.h"

/* Number of bytes to transfer to device per transfer */
#define TRANSFER_SIZE 4096
//#define TRANSFER_SIZE 512
/* Maximum number of transfers to the device to be pending at one time */
#define MAX_PENDING_TRANSFERS 1000

#define SAMPLES_PER_BYTE_READ 2
/* Active low flag directing the FPGA to reset the FIFO */
#define RESET_FIFO_FLAG_BIT 0

static int exitRequested = 0;

static void sigintHandler(int signum)
{
  exitRequested = 1;
}

static void print_usage(void)
{
  fprintf(stdout,
  "Usage: ./sample_pusher [-h] [filename]\n"
  "Options:\n"
  "  [--help -h]     Print usage information and exit.\n"
  "  [filename]      A filename to get samples from. Must be supplied. Bytes\n"
  "                  in file are assumed to be in Piksi format, ie bits =\n"
  "                    [7:5] : sample 0 sign, sample 0 mag 0, sample 0 mag 1\n"
  "                    [4:2] : sample 1 sign, sample 1 mag 0, sample 1 mag 1\n"
  "                    [1:0] : don't care\n"
  "Note : set_fifo_mode must be run before sample_pusher to configure the USB\n"
  "       hardware on the device for FIFO mode. Run set_uart_mode after\n"
  "       sample_pusher to set the device back to UART mode for normal\n"
  "       operation.\n"
  );
  exit(1);
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

int main(int argc, char **argv){
  struct ftdi_context *ftdi;
  int err = 0;
  FILE *fp = NULL;
  char const *infile  = 0;
  exitRequested = 0;
  char *descstring = NULL;

  static const struct option long_opts[] = {
    {"size",       required_argument, NULL, 's'},
    {"help",       no_argument,       NULL, 'h'},
    {NULL,         no_argument,       NULL, 0}
  };

  opterr = 0;
  int c;
  int option_index = 0;
  long int num_samples_to_send = -1;
  while ((c = getopt_long(argc, argv, "hs:", long_opts, &option_index)) != -1)
    switch (c) {
      case 's': {
        num_samples_to_send = parse_size(optarg);
        if (num_samples_to_send <= 0) {
          fprintf(stderr, "Invalid size argument.\n");
          return EXIT_FAILURE;
        }
        break;
      }
      case 'h': {
        print_usage();
        return EXIT_SUCCESS;
        break;
      }
      case '?': {
        if (optopt == 's')
          fprintf(stderr, "Transfer size option requires an argument.\n");
        else
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        return EXIT_FAILURE;
        break;
      }
      default:
        abort();
     }
   
  /* Get name of input file */
  if (optind < argc - 1){
    // Too many extra args
    print_usage();
  } else if (optind == argc - 1){
    // Exactly one extra argument- a dump file
    infile = argv[optind];
  } else {
    fprintf(stderr, "Exiting because no file was specified\n");
    return EXIT_FAILURE;
  }
   
  if ((ftdi = ftdi_new()) == 0){
    fprintf(stderr, "ftdi_new failed\n");
    return EXIT_FAILURE;
  }
   
  if (ftdi_set_interface(ftdi, INTERFACE_A) < 0){
    fprintf(stderr, "ftdi_set_interface failed\n");
    ftdi_free(ftdi);
    return EXIT_FAILURE;
  }
   
  if (ftdi_usb_open_desc(ftdi, 0x0403, 0x8398, descstring, NULL) < 0){
    fprintf(stderr,"Can't open ftdi device: %s\n",ftdi_get_error_string(ftdi));
    ftdi_free(ftdi);
    return EXIT_FAILURE;
  }
   
  /* A timeout value of 1 results in may skipped blocks */
  if(ftdi_set_latency_timer(ftdi, 2)){
    fprintf(stderr,"Can't set latency, Error %s\n",ftdi_get_error_string(ftdi));
    ftdi_usb_close(ftdi);
    ftdi_free(ftdi);
    return EXIT_FAILURE;
  }
   
  if (ftdi_usb_purge_tx_buffer(ftdi) < 0){
    fprintf(stderr,"Can't rx purge %s\n",ftdi_get_error_string(ftdi));
    return EXIT_FAILURE;
  }
  if ((fp = fopen(infile,"r+")) == 0){
    fprintf(stderr,"Can't open sample file %s, Error %s\n", infile, strerror(errno));
    return EXIT_FAILURE;
  }

  /* Exit if there aren't the requested number of samples in the file */
  err = fseek(fp, 0, SEEK_END);
  if (err != 0 ) {
    fprintf(stderr,"Failed to seek to end of file to find file size\n");
    exit(0);
  }
  long int file_size = ftell(fp);
  if (file_size < num_samples_to_send/SAMPLES_PER_BYTE_READ) {
    fprintf(stderr,"Couldn't read enough samples from file\n");
    exit(0);
  }
  /* If size argument isn't specified, transmit the whole file */
  if (num_samples_to_send == -1) { /* -1 means uninitialized */
    num_samples_to_send = file_size * SAMPLES_PER_BYTE_READ;
    fprintf(stdout,"No -s argument specified, using size of file instead : %lu\n", num_samples_to_send);
  }
  rewind(fp);
  /* Floor number of samples to nearest (lower) integer number of transfers to
     number requested */
  long unsigned int num_total_transfers = (num_samples_to_send - (num_samples_to_send % TRANSFER_SIZE)) / TRANSFER_SIZE;
  fprintf(stdout,"Total samples sent will be %lu : %lu transfers with %d samples per transfer\n", num_total_transfers*TRANSFER_SIZE,num_total_transfers,TRANSFER_SIZE);

  /* Set up handler for CTRL+C */
  signal(SIGINT, sigintHandler);

  /* Set transfer size */
  err = ftdi_write_data_set_chunksize(ftdi,TRANSFER_SIZE);
  if (err != 0) {
    fprintf(stderr,"ftdi_write_data_set_chunksize returned an error = %d\n", err);
    return EXIT_FAILURE;
  }

  /* Set chip in synchronous FIFO mode */
  if (ftdi_set_bitmode(ftdi, 0xff, BITMODE_SYNCFF) < 0){
    fprintf(stderr,"Can't set synchronous fifo mode: %s\n",
            ftdi_get_error_string(ftdi));
  }

  unsigned char* bytes_read = malloc(sizeof(unsigned char)*TRANSFER_SIZE/SAMPLES_PER_BYTE_READ);
  unsigned char* bytes_send[MAX_PENDING_TRANSFERS];
  for (uint64_t i = 0; i<MAX_PENDING_TRANSFERS; i++){
    bytes_send[i] = malloc(sizeof(unsigned char)*TRANSFER_SIZE);
  }
  struct ftdi_transfer_control* transfers[MAX_PENDING_TRANSFERS];
  /* For FPGA to cross-check against to make sure it doesn't have dropped
     or inserted samples, counts from 0 to 6 and then rolls over */
  uint8_t err_check_counter = 0;
  uint64_t num_requested_transfers = 0;
  uint64_t num_finished_transfers = 0;
  uint64_t transfer_index = 0;
  uint64_t check_index = 0;
  time_t start = time(NULL);
  time_t last_progress = 0;
  time_t curr_progress;
  /* Set up new transfers if we have room for them in the transfer queue.
     Check if transfers have finished. Exit loop when we have requested
     the total number of transfers. */
  while (num_requested_transfers < num_total_transfers) {
    if (exitRequested == 1) break;
    /* If we have not reached the maximum allowed queued transfers,
       start a new one */
    if (num_requested_transfers - num_finished_transfers < MAX_PENDING_TRANSFERS) {
      /* Read data from file */
      if (fread(bytes_read,1,TRANSFER_SIZE,fp) != TRANSFER_SIZE){
        fprintf(stderr,"Failed to read %d bytes from file\n",TRANSFER_SIZE);
        return EXIT_FAILURE;
      }
      /* Extract samples (Piksi format) from read bytes, OR in 
         err_check_counter, deactivate RESET_FIFO_FLAG bit */
      transfer_index = num_requested_transfers % MAX_PENDING_TRANSFERS;
      for (uint64_t k=0; k<TRANSFER_SIZE/SAMPLES_PER_BYTE_READ; k++) {
        /* Extract first sample, deactivate FIFO_RESET_FLAG high, and OR in 
           error counter */
        bytes_send[transfer_index][k*2+0] = (bytes_read[k] & 0xE0)
                                         | ((err_check_counter & 0x07) << 2)
                                         | (0x01 << RESET_FIFO_FLAG_BIT);
        err_check_counter = (err_check_counter + 1) % 7;
        /* Extract second sample, deactivate FIFO_RESET_FLAG high, and OR in 
           error counter */
        bytes_send[transfer_index][k*2+1] = ((bytes_read[k]<<3) & 0xE0)
                                         | ((err_check_counter & 0x07) << 2)
                                         | (0x01 << RESET_FIFO_FLAG_BIT);
        err_check_counter = (err_check_counter + 1) % 7;
      }
      /* Set the RESET_FIFO_FLAG low (active low) for the first sample
         of the first transfer. */
      if (num_requested_transfers == 0) {
        bytes_send[0][0] &= (~(0x01 << RESET_FIFO_FLAG_BIT));
      }
      /* Submit the transfer request */
      transfers[transfer_index] = ftdi_write_data_submit(ftdi, bytes_send[transfer_index], TRANSFER_SIZE);
      num_requested_transfers += 1;
    }
    /* Check if a transfer has completed. First check if we've submitted at
       least MAX_PENDING_TRANSFERS transfer requests */
    if (num_requested_transfers >= MAX_PENDING_TRANSFERS){
      if (ftdi_transfer_data_done(transfers[check_index]) > 0){
        check_index = (num_finished_transfers+1) % MAX_PENDING_TRANSFERS;
        num_finished_transfers += 1;
      }
    }
    /* Print progress */
    curr_progress = time(NULL);
    if (curr_progress-last_progress > 0) {
      last_progress = curr_progress;
      fprintf(stdout,"Elapsed seconds = %ld : %lu transfers finished - %lu samples\n",curr_progress-start,(long unsigned int)num_finished_transfers,(long unsigned int)num_finished_transfers*TRANSFER_SIZE);
    }
  }

  /* Wait for the remaining transfers to finish */
  while (num_finished_transfers < num_requested_transfers) {
    if (exitRequested == 1) break;
    if (ftdi_transfer_data_done(transfers[check_index]) > 0){
      check_index = (num_finished_transfers+1) % MAX_PENDING_TRANSFERS;
      num_finished_transfers += 1;
    }
    curr_progress = time(NULL);
    if (curr_progress-last_progress > 0) {
      last_progress = curr_progress;
      fprintf(stdout,"Elapsed seconds = %ld : %lu transfers finished - %lu samples\n",curr_progress-start,(long unsigned int)num_finished_transfers,(long unsigned int)num_finished_transfers*TRANSFER_SIZE);
    }
  }

  /* Sample pushing ended, final progress print */
  curr_progress = time(NULL);
  fprintf(stdout,"Elapsed seconds = %ld : %lu transfers finished - %lu samples\n",curr_progress-start,(long unsigned int)num_finished_transfers,(long unsigned int)num_finished_transfers*TRANSFER_SIZE);
  fprintf(stdout,"Sample pushing ended.\n");

  /* Free memory used to read data from file and store samples */
  //why does this cause memory corruption?
  //free(bytes_read);
  //for (uint64_t i = 0; i<MAX_PENDING_TRANSFERS; i++){
  //  free(bytes_send[i]);
  //}

  /* Close file */
  //why does this cause memory corruption?
  //fclose(fp);
  //fp = NULL;
  
  /* Clean up */
  if (ftdi_set_bitmode(ftdi, 0xff, BITMODE_RESET) < 0){
    fprintf(stderr,"Can't set synchronous fifo mode, Error %s\n",ftdi_get_error_string(ftdi));
    ftdi_usb_close(ftdi);
    ftdi_free(ftdi);
    return EXIT_FAILURE;
  }
  ftdi_usb_close(ftdi);
  ftdi_free(ftdi);
  signal(SIGINT, SIG_DFL);
  exit (0);
}
