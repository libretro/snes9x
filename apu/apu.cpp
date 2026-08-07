/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#include <cmath>
#include <vector>
#include "../snes9x.h"
#include "apu.h"
#include "../msu1.h"
#include "../snapshot.h"
#include "../display.h"

#include "bapu/snes/snes.hpp"

static const int APU_DEFAULT_INPUT_RATE = 31950; // ~59.94Hz
static const int APU_SAMPLE_BLOCK       = 48;
static const int APU_NUMERATOR_NTSC     = 15664;
static const int APU_DENOMINATOR_NTSC   = 328125;
static const int APU_NUMERATOR_PAL      = 34176;
static const int APU_DENOMINATOR_PAL    = 709379;

/* The DSP writes stereo pairs straight into this buffer across a frame, and
   S9xDrainAudio hands the caller a pointer to them at end of frame. One NTSC
   frame is ~534 stereo frames and one PAL frame ~641; 2048 leaves room for
   the APU speedup hack and for a frontend that runs a long frame without
   draining. */
static const int LANDING_BUFFER_FRAMES = 2048;
static int16 landing_buffer[LANDING_BUFFER_FRAMES * 2];

namespace SNES {
#include "bapu/dsp/blargg_endian.h"
CPU cpu;
} // namespace SNES

namespace spc {
static apu_callback callback = NULL;
static void *callback_data = NULL;

static bool8 sound_in_sync = true;
static bool8 sound_enabled = false;

static int32 reference_time;
static uint32 remainder;

static const int timing_hack_numerator = 256;
static int timing_hack_denominator = 256;
/* Set these to NTSC for now. Will change to PAL in S9xAPUTimingSetSpeedup
   if necessary on game load. */
static uint32 ratio_numerator = APU_NUMERATOR_NTSC;
static uint32 ratio_denominator = APU_DENOMINATOR_NTSC;

} // namespace spc

static inline int S9xAPUGetClock(int32);
static inline int S9xAPUGetClockRemainder(int32);

/* Hand the caller this frame's samples and reset the DSP cursor.
 *
 * Zero-copy: the pointer goes straight into the frontend's audio callback.
 * That callback is synchronous - it queues into the frontend's own buffer
 * before returning - and the DSP cannot run between the cursor reset here and
 * the callback's read, because DSP catch-up only happens inside
 * S9xAPUExecute, which the port does not call while uploading. So the data
 * stays intact for exactly as long as it needs to.
 *
 * No internal resampler and no per-frame sample-count alignment: whatever the
 * DSP produced this frame is what the frontend gets, and the frontend's own
 * resampler plus dynamic rate control absorb the per-frame variation, which
 * is what DRC is for. */
const int16 * S9xDrainAudio(int *sample_count)
{
    int count = SNES::dsp.spc_dsp.sample_count();

    if (count > LANDING_BUFFER_FRAMES * 2)
        count = LANDING_BUFFER_FRAMES * 2;

    *sample_count = count;

    SNES::dsp.spc_dsp.set_output(landing_buffer, LANDING_BUFFER_FRAMES * 2);

    spc::sound_in_sync = true;

    return landing_buffer;
}

int S9xGetSampleCount(void)
{
    return SNES::dsp.spc_dsp.sample_count();
}

/* The rate the DSP actually emits at, which is what the frontend must be told
   now that the core no longer resamples. 32040 Hz, scaled by the APU speedup
   hack. Integer round-to-nearest: 32040 * 256 fits in 32 bits and the
   denominator is 1..256, so this is exact and keeps the audio path free of
   floating point. */
uint32 S9xGetAudioSampleRate(void)
{
    unsigned denom = (unsigned) spc::timing_hack_denominator;
    return (uint32) (((unsigned) (32040 * 256) + denom / 2) / denom);
}

void S9xLandSamples(void)
{
    if (spc::callback != NULL)
        spc::callback(spc::callback_data);

    spc::sound_in_sync = true;
}

void S9xClearSamples(void)
{
    SNES::dsp.spc_dsp.set_output(landing_buffer, LANDING_BUFFER_FRAMES * 2);
}

void S9xSetSamplesAvailableCallback(apu_callback callback, void *data)
{
    spc::callback = callback;
    spc::callback_data = data;
}

/* Rate conversion is the frontend's job now, so there is no internal ratio to
   keep up to date and dynamic rate control has nothing to steer here: the
   frontend adjusts its own resampler against its own buffer level. Kept as a
   no-op so ports that call it still link. */
void S9xUpdateDynamicRate(int avail, int buffer_size)
{
    (void) avail;
    (void) buffer_size;
}

bool8 S9xInitSound(int buffer_ms)
{
    (void) buffer_ms;

    SNES::dsp.spc_dsp.set_output(landing_buffer, LANDING_BUFFER_FRAMES * 2);

    spc::sound_enabled = true;

    return (spc::sound_enabled);
}

void S9xSetSoundControl(uint8 voice_switch)
{
    SNES::dsp.spc_dsp.set_stereo_switch(voice_switch << 8 | voice_switch);
}

void S9xSetSoundMute(bool8 mute)
{
    Settings.Mute = mute;
    if (!spc::sound_enabled)
        Settings.Mute = true;
}

bool8 S9xInitAPU(void)
{
    S9xClearSamples();

    return true;
}

void S9xDeinitAPU(void)
{
    S9xMSU1DeInit();
}

static inline int S9xAPUGetClock(int32 cpucycles)
{
    return (spc::ratio_numerator * (cpucycles - spc::reference_time) + spc::remainder) /
           spc::ratio_denominator;
}

static inline int S9xAPUGetClockRemainder(int32 cpucycles)
{
    return (spc::ratio_numerator * (cpucycles - spc::reference_time) + spc::remainder) %
           spc::ratio_denominator;
}

uint8 S9xAPUReadPort(int port)
{
    S9xAPUExecute();
    return ((uint8)SNES::smp.port_read(port & 3));
}

void S9xAPUWritePort(int port, uint8 byte)
{
    S9xAPUExecute();
    SNES::cpu.port_write(port & 3, byte);
}

void S9xAPUSetReferenceTime(int32 cpucycles)
{
    spc::reference_time = cpucycles;
}

void S9xAPUExecute(void)
{
    int cycles = S9xAPUGetClock(CPU.Cycles);
    spc::remainder = S9xAPUGetClockRemainder(CPU.Cycles);
    SNES::smp.clock -= cycles;
    SNES::smp.enter();

    S9xAPUSetReferenceTime(CPU.Cycles);
}

void S9xAPUEndScanline(void)
{
    S9xAPUExecute();
    SNES::dsp.synchronize();

    if (SNES::dsp.spc_dsp.sample_count() >= APU_SAMPLE_BLOCK)
        S9xLandSamples();
}

void S9xAPUTimingSetSpeedup(int ticks)
{
    if (ticks != 0)
        printf("APU speedup hack: %d\n", ticks);

    spc::timing_hack_denominator = 256 - ticks;

    spc::ratio_numerator = Settings.PAL ? APU_NUMERATOR_PAL : APU_NUMERATOR_NTSC;
    spc::ratio_denominator = Settings.PAL ? APU_DENOMINATOR_PAL : APU_DENOMINATOR_NTSC;
    spc::ratio_denominator = spc::ratio_denominator * spc::timing_hack_denominator / spc::timing_hack_numerator;
}

void S9xResetAPU(void)
{
    spc::reference_time = 0;
    spc::remainder = 0;

    SNES::cpu.reset();
    SNES::smp.power();
    SNES::dsp.power();

    S9xClearSamples();
}

void S9xSoftResetAPU(void)
{
    spc::reference_time = 0;
    spc::remainder = 0;
    SNES::cpu.reset();
    SNES::smp.reset();
    SNES::dsp.reset();

    S9xClearSamples();
}

void S9xAPUSaveState(uint8 *block)
{
    uint8 *ptr = block;

    SNES::smp.save_state(&ptr);
    SNES::dsp.save_state(&ptr);

    SNES::set_le32(ptr, spc::reference_time);
    ptr += sizeof(int32);
    SNES::set_le32(ptr, spc::remainder);
    ptr += sizeof(int32);
    SNES::set_le32(ptr, SNES::dsp.clock);
    ptr += sizeof(int32);
    memcpy(ptr, SNES::cpu.registers, 4);
    ptr += sizeof(int32);

    memset(ptr, 0, SPC_SAVE_STATE_BLOCK_SIZE - (ptr - block));
}

void S9xAPULoadState(uint8 *block)
{
    uint8 *ptr = block;

    SNES::smp.load_state(&ptr);
    SNES::dsp.load_state(&ptr);
    spc::reference_time = SNES::get_le32(ptr);
    ptr += sizeof(int32);
    spc::remainder = SNES::get_le32(ptr);
    ptr += sizeof(int32);
    SNES::dsp.clock = SNES::get_le32(ptr);
    ptr += sizeof(int32);
    memcpy(SNES::cpu.registers, ptr, 4);
}

static void to_var_from_buf(uint8 **buf, void *var, size_t size)
{
    memcpy(var, *buf, size);
    *buf += size;
}

#undef IF_0_THEN_256
#define IF_0_THEN_256(n) ((uint8)((n)-1) + 1)
void S9xAPULoadBlarggState(uint8 *oldblock)
{
    uint8 *ptr = oldblock;

    SNES::SPC_State_Copier copier(&ptr, to_var_from_buf);

    copier.copy(SNES::smp.apuram, 0x10000); // RAM

    uint8 regs_in[0x10];
    uint8 regs[0x10];
    uint16 pc, spc_time, dsp_time;
    uint8 a, x, y, psw, sp;

    copier.copy(regs, 0x10);    // REGS
    copier.copy(regs_in, 0x10); // REGS_IN

    // CPU Regs
    pc = copier.copy_int(0, sizeof(uint16));
    a = copier.copy_int(0, sizeof(uint8));
    x = copier.copy_int(0, sizeof(uint8));
    y = copier.copy_int(0, sizeof(uint8));
    psw = copier.copy_int(0, sizeof(uint8));
    sp = copier.copy_int(0, sizeof(uint8));
    copier.extra();

    // times
    spc_time = copier.copy_int(0, sizeof(uint16));
    dsp_time = copier.copy_int(0, sizeof(uint16));

    int cur_time = S9xAPUGetClock(CPU.Cycles);

    // spc_time is absolute, dsp_time is relative
    // smp.clock is relative, dsp.clock relative but counting upwards
    SNES::smp.clock = spc_time - cur_time;
    SNES::dsp.clock = -1 * dsp_time;

    // DSP
    SNES::dsp.load_state(&ptr);

    // Timers
    uint16 next_time[3];
    uint8 divider[3], counter[3];
    for (int i = 0; i < 3; i++)
    {
        next_time[i] = copier.copy_int(0, sizeof(uint16));
        divider[i] = copier.copy_int(0, sizeof(uint8));
        counter[i] = copier.copy_int(0, sizeof(uint8));
        copier.extra();
    }
    // construct timers out of available parts from blargg smp
    SNES::smp.timer0.enable = regs[1] >> 0 & 1;        // regs[1] = CONTROL
    SNES::smp.timer0.target = IF_0_THEN_256(regs[10]); // regs[10+i] = TiTARGET
    // blargg counts time, get ticks through timer frequency
    // (assume tempo = 256)
    SNES::smp.timer0.stage1_ticks = 128 - (next_time[0] - cur_time) / 128;
    SNES::smp.timer0.stage2_ticks = divider[0];
    SNES::smp.timer0.stage3_ticks = counter[0];

    SNES::smp.timer1.enable = regs[1] >> 1 & 1;
    SNES::smp.timer1.target = IF_0_THEN_256(regs[11]);
    SNES::smp.timer1.stage1_ticks = 128 - (next_time[1] - cur_time) / 128;
    SNES::smp.timer1.stage2_ticks = divider[0];
    SNES::smp.timer1.stage3_ticks = counter[0];

    SNES::smp.timer2.enable = regs[1] >> 2 & 1;
    SNES::smp.timer2.target = IF_0_THEN_256(regs[12]);
    SNES::smp.timer2.stage1_ticks = 16 - (next_time[2] - cur_time) / 16;
    SNES::smp.timer2.stage2_ticks = divider[0];
    SNES::smp.timer2.stage3_ticks = counter[0];

    copier.extra();

    SNES::smp.opcode_number = 0;
    SNES::smp.opcode_cycle = 0;

    SNES::smp.regs.pc = pc;
    SNES::smp.regs.sp = sp;
    SNES::smp.regs.B.a = a;
    SNES::smp.regs.x = x;
    SNES::smp.regs.B.y = y;

    // blargg's psw has same layout as byuu's flags
    SNES::smp.regs.p = psw;

    // blargg doesn't explicitly store iplrom_enable
    SNES::smp.status.iplrom_enable = regs[1] & 0x80;

    SNES::smp.status.dsp_addr = regs[2];

    SNES::smp.status.ram00f8 = regs_in[8];
    SNES::smp.status.ram00f9 = regs_in[9];

    // default to 0 - we are on an opcode boundary, shouldn't matter
    SNES::smp.rd = SNES::smp.wr = SNES::smp.dp = SNES::smp.sp = SNES::smp.ya = SNES::smp.bit = 0;

    spc::reference_time = SNES::get_le32(ptr);
    ptr += sizeof(int32);
    spc::remainder = SNES::get_le32(ptr);

    // blargg stores CPUIx in regs_in
    memcpy(SNES::cpu.registers, regs_in + 4, 4);
}
