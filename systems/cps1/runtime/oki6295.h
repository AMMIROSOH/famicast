/* The OKI MSM6295: four voices playing 4-bit ADPCM phrases from the sample
 * ROM, listed in a table of 128 at its start. At 1 MHz, a sample every 132
 * clocks with pin 7 high (the CPS-1's at power on), 165 with it low. */
#ifndef CPS1_OKI6295_H
#define CPS1_OKI6295_H

#include <stdint.h>

#define OKI_RATE (1000000 / 132)  /* 7,575 Hz */

typedef struct {
    uint8_t playing;
    uint32_t start, nibble, count;  /* the phrase's first byte; nibbles played and to play */
    int16_t signal;               /* 12 bits */
    int8_t step;                  /* index into the step table, 0-48 */
    uint8_t vol;                  /* out of 32 */
} OkiVoice;

typedef struct {
    const uint8_t *rom;
    uint32_t size;
    OkiVoice v[4];
    int pending;                  /* the phrase the next byte starts, or -1 */
    uint8_t pin7;                 /* 1: 132 clocks a sample, 0: 165 */
} Oki;

void oki_reset(Oki *o, const uint8_t *rom, uint32_t size);
void oki_write(Oki *o, uint8_t v);
uint8_t oki_status(const Oki *o);  /* bits 0-3 the voices playing; bits 4-7 set */
/* The four voices: the sum of signal (12 bits) x volume (out of 32),
 * -262,144..262,016; 65,536 is full scale. */
int oki_sample(Oki *o);

#endif
