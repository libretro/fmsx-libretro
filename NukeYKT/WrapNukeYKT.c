/*
 * Based on https://github.com/openMSX/openMSX/blob/master/src/sound/YM2413OriginalNukeYKT.cc
 * Source for opll.c/h and LICENSE is https://github.com/nukeykt/Nuked-OPLL
 * Both are GPL2 - see LICENSE file in this folder.
 *
 * YM2413 specs:
 * - https://en.wikipedia.org/wiki/Yamaha_YM2413
 * - https://www.msx.org/wiki/MSX-Music_programming
 * - https://www.smspower.org/maxim/Documents/YM2413ApplicationManual
 */
#include "WrapNukeYKT.h"
#include "MSX.h"
#include "Sound.h"

#include <stdint.h>
#include <string.h>

void NukeYKT_Reset2413(YM2413_NukeYKT *opll)
{
  OPLL_Reset(&opll->opll, opll_type_ym2413b);
  opll->port_write_index=0;
  opll->samples[0]=0;
  opll->sample_write_index=1;
}

void NukeYKT_generate_one_channel(YM2413_NukeYKT *opll, int16_t *out)
{
  int32_t buf[2];

  OPLL_Clock(&opll->opll, (int32_t*)&buf);

  /*
   * NukeYKT goes through 18 cycles:
   * - cycles 8-16 generate music 0-5,
   * - cycles 0&1 drums 0&1,
   * - cycles 2-4 drums 2-3 _or_ music 6-8 (one or the other is 0)
   * - cycles 5-7,13&17 nothing
   * -> [m]usic goes to buf[0]
   * -> drum ('[r]hythm') goes to buf[1] at half volume
   *
   * Output range per channel appears to be [-128,+127].
   * Summed output range can theoretically go up to [-1408,+1397] for 6+5 channels.
   * In practice, [-1024,+1023] seems fine, giving 11 bits depth (and allowing for <<5 amplification to get to 16b signed).
   */
  switch (opll->opll.cycles) {
    case  0: *out +=          buf[1] * 2; break; // hihat?
    case  1: *out +=          buf[1] * 2; break; // tom-tom?
    case  2: *out += buf[0] + buf[1] * 2; break; // bass drum?
    case  3: *out += buf[0] + buf[1] * 2; break; // snare drum?
    case  4: *out += buf[0] + buf[1] * 2; break; // top cymbal?
    case  8: *out += buf[0];              break;
    case  9: *out += buf[0];              break;
    case 10: *out += buf[0];              break;
    case 14: *out += buf[0];              break;
    case 15: *out += buf[0];              break;
    case 16: *out += buf[0];              break;
  }
}

void NukeYKT_GenerateChannels(YM2413_NukeYKT *opll, uint32_t n)
{
  int i,l;

  for (l=0;l<n;l++) {
    if (l<opll->port_write_index)
      OPLL_Write(&opll->opll, opll->writes[l].port, opll->writes[l].value);
    // After every port write at least 1 sample must be generated.
    // Go through 18 writes to get exactly one sample per channel - which we add together.
    for (i=0;i<CYCLE_COUNT;i++)
      NukeYKT_generate_one_channel(opll, &opll->samples[opll->sample_write_index]);
    if (opll->sample_write_index < NUM_SAMPLES_PER_FRAME) // 'handle' overflow that happens at initial frame
      opll->sample_write_index++;
  }

  // move any unused writes to index 0
  memcpy(&opll->writes, &opll->writes[l], sizeof(opll->writes)-l*sizeof(Write));
  opll->port_write_index=opll->port_write_index<=l?0:opll->port_write_index-l;
}

void NukeYKT_Sync2413(YM2413_NukeYKT *opll, uint32_t ticks)
{
  uint32_t num_samples_to_request;

  opll->ticksPending+=ticks;
  num_samples_to_request=opll->ticksPending/Z80_CYCLES_PER_SAMPLE;
  opll->ticksPending%=Z80_CYCLES_PER_SAMPLE;

  if (opll->sample_write_index==1)
    memset(&opll->samples[1], 0, sizeof(int16_t)*(NUM_SAMPLES_PER_FRAME+2-1));

  NukeYKT_GenerateChannels(opll, num_samples_to_request);
}

void NukeYKT_WritePort2413(YM2413_NukeYKT *opll, bool port, byte value)
{
  opll->writes[opll->port_write_index].port=port;
  opll->writes[opll->port_write_index].value=value;
  opll->port_write_index++;
  if (opll->port_write_index>=sizeof(opll->writes)/sizeof(Write))
    // wrap around - should only happen when fMSX YM2413 emulation is on. There are 32 slots, and rarely more than 2 are used in practice (with NukeYKT enabled).
    opll->port_write_index=0;
}
