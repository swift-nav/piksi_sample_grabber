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
 *   Purpose : Allows streaming of raw samples from MAX2769 RF Frontend for 
 *             post-processing and analysis. Samples are 3-bit and saved to 
 *             given file one sample per byte as signed integers.
 *
 *   Usage :   After running set_fifo_mode to set the FT232H on the Piksi to 
 *             fifo mode, use sample_pusher (see arguments below).
 *             End sample capture with ^C (CTRL+C). After finishing sample
 *             capture, run set_uart_mode to set the FT232H on the Piksi back
 *             to UART mode for normal operation.
 *
 *   Options : ./sample_pusher [-s number] [-v] [-h] [filename]
 *             [--size -s]     Number of samples to collect before exiting.
 *                             May be suffixed with a k (1e3) or an M (1e6).
 *                             If no argument is supplied, samples will be
 *                             collected until ^C (CTRL+C) is received.
 *             [--verbose -v]  Print more verbose output.
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

#include "ftdi.h"
#include "pipe/pipe.h"

/* Number of bytes to initially read out of device without saving to file */
#define NUM_FLUSH_BYTES 50000
/* Number of samples in each byte received (regardless of how they're packed)*/
#define SAMPLES_PER_BYTE 1
/* Number of bytes to read out of FIFO and write to disk at a time */
#define WRITE_SLICE_SIZE 50 
/* Maximum number of elements in pipe - 0 means size is unconstrained */
#define PIPE_SIZE 0

/* FPGA FIFO Error Flag is 0th bit, active low */
#define FPGA_FIFO_ERROR_CHECK(byte) (!(byte & 0x01))

static FILE *inputFile = NULL;

static int exitRequested = 0;

static int verbose = 0;

static void sigintHandler(int signum)
{
  exitRequested = 1;
}

static void print_usage(void)
{
  printf(
  "Usage: ./sample_pusher [-s num] [-v] [-h] [filename]\n"
  "Options:\n"
  "  [--size -s]     Number of samples to write before exiting. Number may\n"
  "                  be suffixed with a k (1e3) or an M (1e6). If no argument\n"
  "                  is supplied, samples will be written to device until ^C\n"
  "                  (CTRL+C) is received or the end of the file is reached.\n"
  "                  \n"
  "  [--verbose -v]  Print more verbose output.\n"
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
  int err __attribute__((unused)) = 0;
  FILE *fp = NULL;
  char const *infile  = 0;
  inputFile =0;
  exitRequested = 0;
  char *descstring = NULL;

  static const struct option long_opts[] = {
    {"size",       required_argument, NULL, 's'},
    {"verbose",    no_argument,       NULL, 'v'},
    {"help",       no_argument,       NULL, 'h'},
    {NULL,         no_argument,       NULL, 0}
  };

  opterr = 0;
  int c;
  int option_index = 0;
  long int num_samples_to_send = -1;
  while ((c = getopt_long(argc, argv, "vhs:", long_opts, &option_index)) != -1)
    switch (c) {
      case 'v':
        verbose++;
        break;
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
   
   if (optind < argc - 1){
     // Too many extra args
     print_usage();
   } else if (optind == argc - 1){
     // Exactly one extra argument- a dump file
     infile = argv[optind];
   } else {
     if (verbose) {
       fprintf(stderr, "No file name given, will not save samples to file\n");
       return EXIT_FAILURE;
     }
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
   if ((fp = fopen(infile,"r+")) == 0)
     fprintf(stderr,"Can't open sample file %s, Error %s\n", infile, strerror(errno));
   signal(SIGINT, sigintHandler);

   
   unsigned int chunksize = 4096;
//   unsigned int chunksize = 256;
   unsigned int num_tcs = 10000;
   err = ftdi_write_data_set_chunksize(ftdi,chunksize);
   if (err != 0) {
     printf("ftdi_write_data_set_chunksize returned an error = %d\n", err);
   }
   unsigned int bytes_to_read = chunksize*num_tcs;
   unsigned char *write_data = malloc(sizeof(char)*bytes_to_read);
   unsigned int bytes_read = fread(write_data, 1, bytes_to_read, fp);
   if (bytes_read != bytes_to_read) {
     printf("Couldn't read %d bytes from file\n",bytes_to_read);
     exit(1);
   } else {
     printf("Read %d bytes from file\n",bytes_read);
   }
   for (uint32_t i=0; i<bytes_read; i++){
     //mask reset fifo flag
     write_data[i] &= 0x00; //have data count from 0 to 6 then wrap
     write_data[i] |= (i % 7) << 2; //have data count from 0 to 6 then wrap
     write_data[i] |= 0x01;
   }
   struct ftdi_transfer_control* tc[num_tcs];
   //insert reset fifo flag
   write_data[0] = 0x00;
   tc[0] = ftdi_write_data_submit(ftdi, write_data, chunksize);
   //mask reset fifo flag
   write_data[0] |= 0x01;
   printf("first and last 5 of write data starting at %d = ",0);
   for (uint32_t k=0; k<5; k++){
     printf("%d ",((write_data[k] & 0xF0) >> 5));
   }
   for (int32_t k=-5; k<0; k++){
     printf("%d ",((write_data[chunksize + k] & 0xF0) >> 5));
   }
   printf("\n");
   for (uint32_t i=1; i<num_tcs; i++){
     tc[i] = ftdi_write_data_submit(ftdi, write_data + i*chunksize, chunksize);
     printf("first and last 5 of write data starting at %d = ",i);
     for (uint32_t k=0; k<5; k++){
       printf("%d ",(((write_data + i*chunksize)[k] & 0xF0) >> 5));
     }
     for (int32_t k=-5; k<0; k++){
       printf("%d ",(((write_data + i*chunksize)[chunksize + k] & 0xF0) >> 5));
     }
     printf("\n");
   }
   if (ftdi_set_bitmode(ftdi,  0xff, BITMODE_SYNCFF) < 0){
     fprintf(stderr,"Can't set synchronous fifo mode: %s\n",
             ftdi_get_error_string(ftdi));
   }

   printf("waiting for xfers to finish\n");
   for (uint32_t i = 0; i<num_tcs; i++){
     int rc = 0;
     printf("before while loop\n");
     while((rc = ftdi_transfer_data_done(tc[i])) <= 0){
       printf("in while loop\n");
//        if (exitRequested) break;
       if (rc < 0)
         printf("rc[%d] = %d\n", i, rc);
     }
     printf("Xfer %d done\n", i);
   }

   if (verbose) {
     printf("Sample pushing ended.\n");
   }
   
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
