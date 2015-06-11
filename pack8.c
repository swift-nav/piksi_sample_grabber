#include <stdio.h>
#include <stdint.h>

#define CHUNK_N 8192  // number of packed bytes

int main(void) {
  static uint8_t inbuf[CHUNK_N * 8];
  static uint8_t outbuf[CHUNK_N];
  size_t n;
  while ((n = fread(inbuf, 8, CHUNK_N, stdin))) {
    uint8_t *p = inbuf;
    for (size_t i = 0; i < n; i++) {
      uint8_t pack = 0;
      for (int j = 0; j < 8; j++) {
         pack <<= 1;  // Will end up with first sample in MSB
         pack |= (*p++) >> 7;  // MSB of unpacked byte is the sample
      }
      outbuf[i] = pack;
    }
    fwrite(outbuf, 1, n, stdout);
  }
}