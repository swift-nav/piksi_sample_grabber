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

#include "ftdi.h"

#define NUM_FLUSH_BYTES 50000
#define MAX_N_SAMPLES 130*16368000
#define SAMPLES_PER_BYTE 2
#

static uint64_t total_bytes_saved = 0;
static char *file_buf;

/* Samples we get from the device are {sign,msb mag,lsb mag} */
static char sign_mag_mapping[8] = {1, 3, 5, 7, -1, -3, -5, -7};

static FILE *outputFile;

static int exitRequested = 0;

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
usage(const char *argv0)
{
  fprintf(stderr,
         "Usage: %s [options...] \n"
         "Test streaming read from FT2232H\n"
         "[-P string] only look for product with given string\n"
         "\n"
         "If some filename is given, write data read to that file\n"
         "Progess information is printed each second\n"
         "Abort with ^C\n"
         "\n"
         "Options:\n"
         "\n"
         "Copyright (C) 2009 Micah Dowty <micah@navi.cx>\n"
         "Adapted for use with libftdi (C) 2010 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>\n",
         argv0);
  exit(1);
}

static uint32_t n_err = 0;
static int
readCallback(uint8_t *buffer, int length, FTDIProgressInfo *progress, void *userdata)
{
  /*
   * Keep track of number of packets read - don't record samples until we have 
   * read a large number of packets. This is to flush out the FIFO in the FPGA 
   * - it is necessary to do this to ensure we receive continuous samples.
   */
  static uint64_t total_num_bytes_received = 0;
  /* If we are going over the length of our file buffer then stop here */
  if (SAMPLES_PER_BYTE*(total_bytes_saved + length) >= MAX_N_SAMPLES){
    exitRequested = 1;
  } else{
    if (length){
       total_num_bytes_received += length;
       if (total_num_bytes_received >= NUM_FLUSH_BYTES){
         /* Save data if we have a file to write it to */
         if (outputFile) {
           uint64_t si;
           for (si = 0; si < length; si++){
             /* Check data to see if a FIFO error occured */
             if (!(buffer[si] & 0x01)) {
               perror("FPGA FIFO Overflow Flag");
               printf("num samples taken = %ld\n",
                      (long int)(total_bytes_saved+si));
               while(1);
             }
             file_buf[total_bytes_saved + si] = buffer[si];
           }
           total_bytes_saved += length;
         }
       }
     }
     if (progress){
       fprintf(stderr, "%10.02fs total time %9.3f MiB captured %7.1f kB/s curr rate %7.1f kB/s totalrate %d dropouts\n",
               progress->totalTime,
               progress->current.totalBytes / (1024.0 * 1024.0),
               progress->currentRate / 1024.0,
               progress->totalRate / 1024.0,
               n_err);
     }
   }
   return exitRequested ? 1 : 0;
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
       usage(argv[0]);
     }
   
   if (optind == argc - 1){
     // Exactly one extra argument- a dump file
     outfile = argv[optind];
   }
   else if (optind < argc){
     // Too many extra args
     usage(argv[0]);
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

   /* Only allocate memory for file buffer if we are going to write to a file */
   if (outputFile) {
     file_buf = (char *)malloc(MAX_N_SAMPLES/SAMPLES_PER_BYTE);
   }
   
   err = ftdi_readstream(ftdi, readCallback, NULL, 8, 256);
   if (err < 0 && !exitRequested)
     exit(1);

   /* Write samples to file
    * Extract samples from buffer and convert from signmag to signed
    * Packing of each byte is
    *   [7:5] : Sample 0
    *   [4:2] : Sample 1
    *   [1] : Unused
    *   [0] : Error flag (FIFO full, over/underflow), active low
    */
   uint64_t wi;
   if (outputFile) {
     for (wi = 0; wi < total_bytes_saved; wi++) {
       /* Write first sample */
       if (fputc(sign_mag_mapping[file_buf[wi]>>5 & 0x07],outputFile) == EOF){
         perror("Write error");
         while(1);
       }
       /* Write second sample */
       if (fputc(sign_mag_mapping[file_buf[wi]>>2 & 0x07],outputFile) == EOF){
         perror("Write error");
         while(1);
       }
     }
   }
   free(file_buf);
   
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
