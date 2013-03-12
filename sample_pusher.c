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

//static uint64_t total_unflushed_bytes = 0;
static long int bytes_wanted = 0; /* 0 means uninitialized */

static FILE *inputFile = NULL;

static int exitRequested = 0;

/* Pipe specific structs and pointers */
//static pipe_t *sample_pipe;
//static pthread_t file_writing_thread;
//static pipe_producer_t* pipe_writer;
//static pipe_consumer_t* pipe_reader;

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

//static int readCallback(uint8_t *buffer, int length, FTDIProgressInfo *progress, void *userdata)
//{
//  /*
//   * Keep track of number of bytes read - don't record samples until we have 
//   * read a large number of bytes. This is to flush out the FIFO in the FPGA 
//   * - it is necessary to do this to ensure we receive continuous samples.
//   */
//  static uint64_t total_num_bytes_received = 0;
//
//  /* Array for extraction of samples from buffer */
//  char *pack_buffer = (char *)malloc(length*SAMPLES_PER_BYTE*sizeof(char));
//  if (length){
//    if (total_num_bytes_received >= NUM_FLUSH_BYTES){
//      /* Save data to our pipe */
//      if (outputFile) {
//        /*
//         * Convert samples from buffer from signmag to signed and write to 
//         * the pipe. They will be read from the pipe and written to disk in a 
//         * different thread.
//         * Packing of each byte is
//         *   [7:5] : Sample (sign, magnitude high, magnitude low)
//         *   [4:1] : Unused
//         *   [0] : FPGA FIFO Error flag, active low. Usually indicates bytes
//         *         are not being read out of FPGA FIFO quickly enough to avoid
//         *         overflow
//         */
//        if ((length % 2) != 0) {
//          printf("received callback with buffer length not an even number\n");
//          exitRequested = 1;
//        } else {
//          for (uint64_t ci = 0; ci < length/2; ci++){
//            /* Check byte to see if a FIFO error occured */
//            if (FPGA_FIFO_ERROR_CHECK(buffer[ci])) {
//              printf("FPGA FIFO Error Flag at sample number %ld\n",
//                     (long int)(total_unflushed_bytes+ci));
//              exitRequested = 1;
//            }
//            /* Two samples (bytes) at a time */
//            pack_buffer[ci] = (buffer[ci*2+0] & 0xE0) | 
//                              ((buffer[ci*2+1]>>3) & 0x1C) | 
//                              (buffer[ci*2] & 0x01);
//          }
//          /* Push values into the pipe */
//          pipe_push(pipe_writer,(void *)pack_buffer,length/2);
//        }
//      }
//      total_unflushed_bytes += length;
//    }
//    total_num_bytes_received += length;
//  }
//
//  /* bytes_wanted = 0 means program was not run with a size argument */
//  if (bytes_wanted != 0 && total_unflushed_bytes >= bytes_wanted){
//    exitRequested = 1;
//  }
//
//  /* Print progress : time elapsed, bytes transferred, transfer rate */
//  if (verbose) {
//    if (progress){
//      printf("%10.02fs total time %9.3f MiB captured %7.1f kB/s curr %7.1f kB/s total\n",
//              progress->totalTime,
//              progress->current.totalBytes / (1024.0 * 1024.0),
//              progress->currentRate / 1024.0,
//              progress->totalRate / 1024.0);
//    }
//  }
//
//  return exitRequested ? 1 : 0;
//}

//static void* file_writer(void* pc_ptr){
//  pipe_consumer_t* reader = pc_ptr;
//  char buf[WRITE_SLICE_SIZE];
//  size_t bytes_read;
//  while (!(exitRequested)){
//    bytes_read = pipe_pop(reader,buf,WRITE_SLICE_SIZE);
//    if (bytes_read > 0){
//      if (fwrite(buf,bytes_read,1,outputFile) != 1){
//        perror("Write error\n");
//        exitRequested = 1;
//      }
//    }
//  }
//  return NULL;
//}

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
  while ((c = getopt_long(argc, argv, "s:vh", long_opts, &option_index)) != -1)
    switch (c) {
      case 'v':
        verbose++;
        break;
      case 's': {
        long int samples_wanted = parse_size(optarg);
        if (samples_wanted <= 0) {
          fprintf(stderr, "Invalid size argument.\n");
          return EXIT_FAILURE;
        }
        bytes_wanted = samples_wanted / SAMPLES_PER_BYTE;
        if (bytes_wanted <= 0) {
          fprintf(stderr, "Invalid number of bytes to transfer.\n");
          return EXIT_FAILURE;
        }
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

//   sample_pipe = pipe_new(sizeof(char),PIPE_SIZE);
//   pipe_writer = pipe_producer_new(sample_pipe);
//   pipe_reader = pipe_consumer_new(sample_pipe);
//   pipe_free(sample_pipe);

   /* Start thread for writing samples to file */
//   pthread_create(&file_writing_thread, NULL, &file_writer, pipe_reader);
   
   unsigned int chunksize = 4096;
//   unsigned int chunksize = 256;
   unsigned int num_tcs = 1000;
   err = ftdi_write_data_set_chunksize(ftdi,chunksize);
//   if (err != 0) {
//     printf("ftdi_write_data_set_chunksize returned an error = %d\n", err);
//   }
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
     write_data[i] |= (i % 7) << 5; //have data count from 0 to 6 then wrap
     write_data[i] |= 0x04;
//     printf("write_data[%d] = %02x\n",i,write_data[i]);
   }

//   err = ftdi_write_data(ftdi, write_data, bytes_to_read);
   struct ftdi_transfer_control* tc[num_tcs];
   //insert reset fifo flag
   write_data[0] = 0x00;
   tc[0] = ftdi_write_data_submit(ftdi, write_data, chunksize);
   //mask reset fifo flag
   write_data[0] |= 0x04;
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

   /* Close thread and free pipe pointers */
//   pthread_join(file_writing_thread,NULL);
//   pipe_producer_free(pipe_writer);
//   pipe_consumer_free(pipe_reader);
   
   /* Close file */
//   if (inputFile) {
//     fclose(inputFile);
//     inputFile = NULL;
//   }
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
