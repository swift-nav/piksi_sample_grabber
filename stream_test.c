/* stream_test.c
 *
 * Test reading  from FT2232H in synchronous FIFO mode.
 *
 * The FT2232H must supply data due to an appropriate circuit
 *
 * To check for skipped block with appended code, 
 *     a structure as follows is assumed
 * 1* uint32_t num (incremented in 0x4000 steps)
 * 3* uint32_t dont_care
 *
 * After start, data will be read in streaming until the program is aborted
 * Progess information wil be printed out
 * If a filename is given on the command line, the data read will be
 * written to that file
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>

#include "ftdi.h"
#include "pipe/pipe.h"

#define NUM_FLUSH_BYTES 50000
#define SAMPLES_PER_BYTE 2
/* Number of bytes to read out of FIFO and write to disk at a time */
#define WRITE_SLICE_SIZE 50 
#define PIPE_SIZE 0 /* 0 means size is unconstrained */

/* FPGA FIFO Error Flag is 0th bit, active low */
#define FPGA_FIFO_ERROR_CHECK(byte) (!(byte & 0x01))

static uint64_t total_bytes_saved = 0;

/* 
 * Samples we get from the device are {sign,msb mag,lsb mag}. Can index this 
 * array with each received signmag sample to convert to signed (signed int 8)
 */
static const char mapping[8] = {1, 3, 5, 7, -1, -3, -5, -7};

static FILE *outputFile;

static int exitRequested = 0;

/* Pipe specific structs and pointers */
static pipe_t *sample_pipe;
static pthread_t thread;
static pipe_producer_t* pipe_writer;
static pipe_consumer_t* pipe_reader;

/*
 * sigintHandler --
 *
 *    SIGINT handler, so we can gracefully exit when the user hits ctrl-C.
 */
static void
sigintHandler(int signum)
{
  exitRequested = 1;
}

static void
usage(void)
{
  printf("  Usage: ./stream_test [filename] \n"
         "  If some filename is given, write data read to that file\n"
         "  Progess information is printed each second\n"
         "  Abort with ^C\n"
         "  Copyright (C) 2009 Micah Dowty <micah@navi.cx>\n"
         "  Adapted for libftdi (C) 2010 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>\n"
         "  Adapted for use with the Piksi (C) 2013 Swift Navigation <colin@swift-nav.com>\n"
         );
  exit(1);
}

static uint32_t n_err = 0;
static int
readCallback(uint8_t *buffer, int length, FTDIProgressInfo *progress, void *userdata)
{
  /*
   * Keep track of number of bytes read - don't record samples until we have 
   * read a large number of bytes. This is to flush out the FIFO in the FPGA 
   * - it is necessary to do this to ensure we receive continuous samples.
   */
  static uint64_t total_num_bytes_received = 0;
  /* Array for extraction of samples from buffer */
  char *conv_buf = (char *)malloc(length*SAMPLES_PER_BYTE*sizeof(char));
  if (length){
    if (total_num_bytes_received >= NUM_FLUSH_BYTES){
      /* Save data to our pipe */
      if (outputFile) {
        /*
         * Convert samples from buffer from signmag to signed and write to 
         * the pipe. They will be read from the pipe and written to disk in a 
         * different thread.
         * Packing of each byte is
         *   [7:5] : Sample 0
         *   [4:2] : Sample 1
         *   [1] : Unused
         *   [0] : FPGA FIFO Error flag, active low. Usually indicates bytes
         *         are not being read out of FPGA FIFO quickly enough to avoid
         *         overflow
         */
        for (uint64_t ci = 0; ci < length; ci++){
          /* Check byte to see if a FIFO error occured */
          if (FPGA_FIFO_ERROR_CHECK(buffer[ci])) {
            perror("FPGA FIFO Error Flag");
            printf("num samples taken = %ld\n",
                   (long int)(total_bytes_saved+ci));
            exitRequested = 1;
          }
          /* Extract samples from buffer, convert, and store in conv_buf */
          /* First sample */
          conv_buf[ci*2+0] = mapping[(buffer[ci] >> 5) & 0x07];
          /* Second sample */
          conv_buf[ci*2+1] = mapping[(buffer[ci] >> 2) & 0x07];
        }
        /* Push values into the pipe */
        pipe_push(pipe_writer,(void *)conv_buf,length*2);
        total_bytes_saved += length;
      }
    }
    total_num_bytes_received += length;
  }
  if (progress){
    fprintf(stderr, "%10.02fs total time %9.3f MiB captured %7.1f kB/s curr rate %7.1f kB/s totalrate %d dropouts\n",
            progress->totalTime,
            progress->current.totalBytes / (1024.0 * 1024.0),
            progress->currentRate / 1024.0,
            progress->totalRate / 1024.0,
            n_err);
  }
  return exitRequested ? 1 : 0;
}

static void* file_writer(void* pc_ptr){
  pipe_consumer_t* reader = pc_ptr;
  char buf[WRITE_SLICE_SIZE];
  size_t bytes_read;
  while (!(exitRequested)){
    bytes_read = pipe_pop(reader,buf,WRITE_SLICE_SIZE);
    if (bytes_read > 0){
      if (fwrite(buf,bytes_read,1,outputFile) != 1){
        perror("Write error\n");
        exitRequested = 1;
      }
    }
  }
  return NULL;
}

int main(int argc, char **argv){
   struct ftdi_context *ftdi;
   int err, c;
   FILE *of = NULL;
   char const *outfile  = 0;
   outputFile =0;
   exitRequested = 0;
   char *descstring = NULL;
   int option_index;
   static struct option long_options[] = {{NULL},};

   while ((c = getopt_long(argc, argv, "P:n", long_options, &option_index)) !=- 1)
     switch (c) 
     {
     case -1:
       break;
     case 'P':
       descstring = optarg;
       break;
     default:
       usage();
     }
   
   if (optind == argc - 1){
     // Exactly one extra argument- a dump file
     outfile = argv[optind];
   }
   else if (optind < argc){
     // Too many extra args
     usage();
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
   
   if (ftdi_usb_purge_rx_buffer(ftdi) < 0){
     fprintf(stderr,"Can't rx purge %s\n",ftdi_get_error_string(ftdi));
     return EXIT_FAILURE;
   }
   if (outfile)
     if ((of = fopen(outfile,"w+")) == 0)
       fprintf(stderr,"Can't open logfile %s, Error %s\n", outfile, strerror(errno));
   if (of)
     if (setvbuf(of, NULL, _IOFBF , 1<<16) == 0)
       outputFile = of;
   signal(SIGINT, sigintHandler);

   /* Only create pipe if we have a file to write samples to */
   if (outputFile) {
     sample_pipe = pipe_new(sizeof(char),PIPE_SIZE);
     pipe_writer = pipe_producer_new(sample_pipe);
     pipe_reader = pipe_consumer_new(sample_pipe);
     pipe_free(sample_pipe);
   }

   /* Start thread for writing samples to file */
   if (outputFile) {
     pthread_create(&thread, NULL, &file_writer, pipe_reader);
   }
   
   /* Read samples from the device */
   err = ftdi_readstream(ftdi, readCallback, NULL, 8, 256);
   if (err < 0 && !exitRequested)
     exit(1);

   /* Close thread and free pipe pointers */
   if (outputFile) {
     pthread_join(thread,NULL);
     pipe_producer_free(pipe_writer);
     pipe_consumer_free(pipe_reader);
   }
   
   /* Close file */
   if (outputFile) {
     fclose(outputFile);
     outputFile = NULL;
   }
   fprintf(stderr, "Capture ended.\n");
   
   if (ftdi_set_bitmode(ftdi,  0xff, BITMODE_RESET) < 0){
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
