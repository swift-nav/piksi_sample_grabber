#include <stdio.h>
#include <stdint.h>

#define CHUNK_N 8192  // number of packed bytes

int main(void) {
  static uint8_t inbuf[CHUNK_N * 4];
  static uint8_t outbuf[CHUNK_N];
  size_t n;
  while ((n = fread(inbuf, 4, CHUNK_N, stdin))) {
    uint8_t *p = inbuf;
    for (size_t i = 0; i < n; i++) {
      uint8_t pack = 0;
      for (int j = 0; j < 4; j++) {
        pack <<= 2;  // Will end up with first sample in MSB of packed output
        pack |= ((*p) & 0x80) >> 6;  // First sample sign in MSB of byte from piksi
        pack |= ((*p) & 0x10) >> 4;  // Second sample sign in bit 4
        p++;
      }
      outbuf[i] = pack;
    }
    fwrite(outbuf, 1, n, stdout);
  }
}