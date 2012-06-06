#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ftd2xx.h>

#define XFER_LEN (16*1024)
#define MBS 256
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
    /*FT_Purge(ftHandle, FT_PURGE_RX);*/

    unsigned int BytesReceived;
    unsigned long totalBytesReceived = 0;
    unsigned long lastError = 0;

    time_t t0, t1;
    int x = -1;
    t0 = time(NULL);

    while (totalBytesReceived < MBS*1000*1000) {
      ftStatus = FT_Read(ftHandle, RxBuffer, XFER_LEN, &BytesReceived);
      if (ftStatus != FT_OK) {
        printf("Error!\n");
        FT_Close(ftHandle);
        return 1;
      }
      totalBytesReceived += BytesReceived;

      if (x < 0)
        x = (RxBuffer[0]>>5);
      for (int i=0; i<XFER_LEN; i++) {
        if ((RxBuffer[i]>>5) != x) {
          /*if ((totalBytesReceived - BytesReceived + i - lastError) % 510 != 0)*/
          /*if ((RxBuffer[i]>>5) != 0)*/
          {
            printf("\nError @ %lu d %lu, %u -> %u\n",
                totalBytesReceived - BytesReceived + i,
                totalBytesReceived - BytesReceived + i - lastError,
                x, (RxBuffer[i]>>5)
            );

            for (int j=i-10; j<i+10; j++) {
              if (j==i)
                printf("\033[31m");
              if (j==i+1)
                printf("\033[0m");
              printf("%02X ", RxBuffer[j] >> 5);
            }
            printf("\n\n");

          }
          /*else*/
            /*printf("!");*/
          lastError = totalBytesReceived - BytesReceived + i;
          x = (RxBuffer[i]>>5);
        }
        x++;
        if (x > 6) x = 0;
      }

      /*for (i++; i<10*1024; i++) {*/
        /*printf("%02X ", RxBuffer[i] >> 5);*/
        /*if (++n % 20 == 0)*/
          /*printf("\n");*/
      /*}*/

      /*return 0;*/
      /*break;*/
    }

    t1 = time(NULL);
    int t = (int)(t1-t0);
    printf("%d MiB in %d seconds, %g MiB/s\n", MBS, t, MBS / (double)t);

    FT_Close(ftHandle);
  } else {
    printf("Unable to open device!\n");
    return 1;
  }

  return 0;
}

