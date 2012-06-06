#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ftd2xx.h>

#define XFER_LEN (16*1024)
#define MBS 16
unsigned char RxBuffer[XFER_LEN];

int main()
{
  FT_STATUS ftStatus;
  FT_HANDLE ftHandle;

  ftStatus = FT_SetVIDPID(0x0403, 0x8398);
  if (ftStatus != FT_OK) {
    printf("Error setting custom PID!\n");
    return 1;
  }

  FILE* fp = fopen("tehdataz", "w");

  ftStatus = FT_OpenEx("Piksi Passthrough", FT_OPEN_BY_DESCRIPTION, &ftHandle);

  if (ftStatus == FT_OK) {
    printf("Device opened!\n");

    /*FT_EraseEE(ftHandle);*/

    ftStatus = FT_SetBitMode(ftHandle, 0xFF, FT_BITMODE_SYNC_FIFO);
    if (ftStatus != FT_OK) {
      printf("Error setting bit mode!\n");
      FT_Close(ftHandle);
      return 1;
    }
    ftStatus = FT_SetLatencyTimer(ftHandle, 2);
    ftStatus = FT_SetUSBParameters(ftHandle, 0x10000, 0x10000);
    ftStatus = FT_SetFlowControl(ftHandle, FT_FLOW_RTS_CTS, 0, 0);
    FT_Purge(ftHandle, FT_PURGE_RX);

    unsigned int BytesReceived;
    unsigned long totalBytesReceived = 0;
    unsigned long lastError = 0;

    time_t t0, t1;
    t0 = time(NULL);

    /* Flush internal NAP buffers - discard a bunch of data. */
    for (int i=0; i<8; i++)
      ftStatus = FT_Read(ftHandle, RxBuffer, XFER_LEN, &BytesReceived);

    while (totalBytesReceived < MBS*1000*1000) {
      ftStatus = FT_Read(ftHandle, RxBuffer, XFER_LEN, &BytesReceived);
      if (ftStatus != FT_OK) {
        printf("Error!\n");
        FT_Close(ftHandle);
        return 1;
      }
      totalBytesReceived += BytesReceived;
      for (int i=0; i<XFER_LEN; i++) {
        fputc(RxBuffer[i]>>5, fp);
        fputc(0, fp);
      }
    }

    t1 = time(NULL);
    int t = (int)(t1-t0);
    printf("%d MiB in %d seconds, %g MiB/s\n", MBS, t, MBS / (double)t);

    FT_Close(ftHandle);
  } else {
    printf("Unable to open device!\n");
    return 1;
  }

  fclose(fp);

  return 0;
}

