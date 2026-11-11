/* timing.c - wall clock for the engine.

   The gcc build uses clock(). The 16-bit DOS build reads the 8253/8254 system
   timer (channel 0) directly, because Watcom's clock() only steps with the
   18.2 Hz BIOS tick (54.9 ms) - too coarse to time a few thousand cycles.

   Channel 0 generates that tick from the 14.31818 MHz / 12 = 1.193182 MHz PIT
   clock. The BIOS programs it in mode 3 (square wave) with divisor 65536; in
   mode 3 the counter is decremented by 2 and reloads halfway through the tick,
   so a latched value cannot say which half of the tick it is in. timing_init
   reprograms the same channel to mode 2 with the same divisor: the IRQ0 rate,
   the BIOS tick and DOS's clock are unchanged, but the counter now counts down
   by one per 0.838 us clock. Pairing a latched count with the BIOS tick then
   gives a monotonic clock whose phase inside a tick is 0.838 us.

   Tick and phase come from the same counter, so they wrap together. The only
   moment a sample can pair an old tick with a new epoch's phase is while the
   counter wraps between our two phase reads; the read is retried then. If
   channel 0 does not count at all, the BIOS mode is restored and clock() is
   used instead. */

#include "engine.h"

#if defined(__WATCOMC__) && !defined(__386__)

#include <dos.h>
#include <conio.h>

#define PIT_CMD 0x43
#define PIT_CH0 0x40

static u8 timer_ok;                    /* 0 = use clock() */

static u16 pit0_latch(void) {
    u16 v;

    outp(PIT_CMD, 0x00);               /* counter latch command, channel 0 */
    v = (u16)inp(PIT_CH0);             /* LSB */
    v |= (u16)((u16)inp(PIT_CH0) << 8);/* MSB */
    return v;
}

u32 timing_bios_ticks(void) {
    union REGS in, out;

    in.h.ah = 0;                       /* ticks since midnight: CX:DX */
    int86(0x1a, &in, &out);
    return ((u32)out.x.cx << 16) | out.x.dx;
}

/* BIOS tick plus the phase inside the current tick. The tick is read before
   and after the first phase read: a BIOS tick that is pending when the first
   int 1A runs (the counter wrapped just before it) would otherwise pair the
   old tick with the new epoch's phase. If the tick changed, or the counter
   wrapped between the two phase reads, retry. */
static u32 tick_and_phase(u16 *phase) {
    u16 c1, c2;
    u32 t1, t2;

    for (;;) {
        t1 = timing_bios_ticks();
        c1 = pit0_latch();
        t2 = timing_bios_ticks();
        c2 = pit0_latch();
        if (t1 == t2 && (u16)(0u - c2) >= (u16)(0u - c1)) {
            *phase = (u16)(0u - c2);
            return t2;
        }
    }
}

/* One-time: mode 2, LSB then MSB, binary, divisor 65536. Verify that the
   counter moves; otherwise restore the BIOS square wave and keep clock(). */
void timing_init(void) {
    u16 c0, c1;
    i16 i;

    outp(PIT_CMD, 0x34);
    outp(PIT_CH0, 0x00);
    outp(PIT_CH0, 0x00);

    c0 = pit0_latch();
    for (i = 0; i < 16; i++) {
        c1 = pit0_latch();
        if (c1 != c0) { timer_ok = 1; return; }
    }
    outp(PIT_CMD, 0x36);
    outp(PIT_CH0, 0x00);
    outp(PIT_CH0, 0x00);
    timer_ok = 0;
}

clock_t engine_clock(void) {
    u16 phase;
    u32 t;

    if (!timer_ok) return clock();
    t = tick_and_phase(&phase);
    /* One BIOS tick is 65536 PIT counts = 65536 * 1000 / 1193182 = 54.925 ms.
       2197/40 reproduces that to 7.5 ppm with 32-bit arithmetic that cannot
       overflow for a day's tick count; the phase term uses the same scale per
       count (2197 / (40 * 65536)). Both terms are monotonic together. */
    return (clock_t)((t * 2197UL) / 40UL
                     + (phase * 2197UL) / 2621440UL);
}

/* Low 32 bits of the PIT count since the epoch: 0.838 us each, wraps after
   ~60 min. Calibration code subtracts two of these for an exact interval. */
u32 timing_counts(void) {
    u16 phase;
    u32 t;

    if (!timer_ok) return (u32)clock() * 1193UL;
    t = tick_and_phase(&phase);
    return ((t & 0xFFFFUL) << 16) | phase;
}

/* Fast search clock: each call is one channel-0 latch, extended in
   software. The caller anchors once per move and must poll at least once per
   counter period (65536 counts = 54.9 ms); a missed period only makes the
   clock lag, which can delay a stop but never advance it. */
static u32 search_count;               /* extended 1.193 MHz count */
static u16 search_raw;                 /* last raw counter sample */

i16 timing_search_start(void) {
    u16 phase;
    u32 t;

    if (!timer_ok) return 0;
    t = tick_and_phase(&phase);
    search_count = ((t & 0xFFFFUL) << 16) | phase;
    search_raw = (u16)(0u - phase);
    return 1;
}

u32 timing_search_counts(void) {
    u16 raw, d;

    if (!timer_ok) return search_count;
    raw = pit0_latch();
    d = (u16)(search_raw - raw);
    search_raw = raw;
    search_count += d;
    return search_count;
}

#else /* gcc / 32-bit: the host clock is already fine-grained enough */

void timing_init(void) { }

clock_t engine_clock(void) { return clock(); }

/* TSC for the host microbenchmarks. clock() on a modern host resolves a few
   thousand calls as 0 ms; the time-stamp counter is cycle-exact and monotonic,
   which is what these per-call comparisons need. The TSC runs at a constant
   reference rate, so its ticks track wall time even when the core clock boosts;
   the numbers are therefore not core cycles, just a stable host yardstick. */
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#include <x86intrin.h>
u64 timing_cycles(void) { return (u64)__rdtsc(); }
#else
u64 timing_cycles(void) { return (u64)clock() * 1000ULL; }
#endif

u32 timing_counts(void) {
    return (u32)clock() * 1193UL;      /* approximation of PIT counts */
}

i16 timing_search_start(void) { return 0; }
u32 timing_search_counts(void) { return 0; }

#endif
