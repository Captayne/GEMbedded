/*
 * stepdemo.c - pTOS real-time demonstrator: one program, two halves
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * The normal half (main, on the system core) feeds a list of moves into
 * a queue and, at the same time, keeps the system core busy: it reads a
 * file from the SD card over and over and prints progress.
 *
 * The real-time half (rt_init, rt_cycle, on the real-time core, every
 * millisecond) turns the moves into acceleration ramps and feeds a PIO
 * state machine that produces the STEP and DIR signals of a stepper
 * driver in hardware.
 *
 * Proof without an oscilloscope: a second PIO state machine counts the
 * pulses that really appear on the STEP pin (it reads the pad, whoever
 * drives it), and the PIO reports every time its FIFO
 * ran dry while a move was in progress (an underrun).  At the end:
 * commanded steps == generated steps == counted edges, 0 underruns --
 * however busy the system core was.
 *
 *   STEP  GPIO5
 *   DIR   GPIO4
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include "rtx.h"

/* ==== hardware (only touched by the real-time half) ==================== */

#define STEP_PIN        5
#define DIR_PIN         4

#define REG(a)          (*(volatile unsigned long *)(a))
#define SET(a)          REG((a) + 0x2000)       /* atomic set alias */
#define CLR(a)          REG((a) + 0x3000)       /* atomic clear alias */

#define RESETS          0x40020000UL
#define RESETS_DONE     (RESETS + 0x08)
#define RESET_PIO0      (1UL << 11)

#define IO_CTRL(n)      (0x40028000UL + 0x04 + 8 * (n))
#define PAD(n)          (0x40038000UL + 0x04 + 4 * (n))
#define PAD_ISO         0x100UL
#define PAD_OD          0x080UL
#define PAD_IE          0x040UL
#define FUNC_PIO0       6

#define PIO0            0x50200000UL
#define PIO_CTRL        (PIO0 + 0x000)
#define PIO_FSTAT       (PIO0 + 0x004)
#define PIO_FDEBUG      (PIO0 + 0x008)
#define PIO_TXF0        (PIO0 + 0x010)
#define PIO_RXF1        (PIO0 + 0x024)
#define PIO_INSTR_MEM(i) (PIO0 + 0x048 + 4 * (i))
#define PIO_SM0_CLKDIV  (PIO0 + 0x0c8)
#define PIO_SM0_EXECCTRL (PIO0 + 0x0cc)
#define PIO_SM0_SHIFTCTRL (PIO0 + 0x0d0)
#define PIO_SM0_INSTR   (PIO0 + 0x0d8)
#define PIO_SM0_PINCTRL (PIO0 + 0x0dc)
#define PIO_SM1_CLKDIV  (PIO0 + 0x0e0)
#define PIO_SM1_EXECCTRL (PIO0 + 0x0e4)
#define PIO_SM1_SHIFTCTRL (PIO0 + 0x0e8)
#define PIO_SM1_INSTR   (PIO0 + 0x0f0)
#define PIO_SM1_PINCTRL (PIO0 + 0x0f4)
#define FSTAT_RXEMPTY1  (1UL << 9)
#define FSTAT_TXFULL0   (1UL << 16)
#define FDEBUG_TXSTALL0 (1UL << 24)


/*
 * PIO program, clocked at 10 MHz (0.1 us per cycle).  Each FIFO word is
 * one time slice:
 *   bit 31      1 = steps, 0 = pause
 *   bit 30      direction
 *   bits 29-16  count - 1 (steps, or pause loops)
 *   bits 15-0   half period d; a step takes 2d + 7 cycles, a pause loop
 *               2d + 5
 */
static const unsigned short stepper_prog[] = {
    0x80a0,     /*  0: pull block                         */
    0x6041,     /*  1: out y, 1         ; steps or pause  */
    0x6001,     /*  2: out pins, 1      ; DIR             */
    0x602e,     /*  3: out x, 14        ; count - 1       */
    0x60d0,     /*  4: out isr, 16      ; half period     */
    0x006e,     /*  5: jmp !y, 14                         */
    0xe001,     /*  6: set pins, 1      ; STEP high       */
    0xa046,     /*  7: mov y, isr                         */
    0x0088,     /*  8: jmp y--, 8                         */
    0xe000,     /*  9: set pins, 0      ; STEP low        */
    0xa046,     /* 10: mov y, isr                         */
    0x008b,     /* 11: jmp y--, 11                        */
    0x0046,     /* 12: jmp x--, 6                         */
    0x0000,     /* 13: jmp 0                              */
    0xa046,     /* 14: mov y, isr       ; pause           */
    0x008f,     /* 15: jmp y--, 15                        */
    0xa046,     /* 16: mov y, isr                         */
    0x0091,     /* 17: jmp y--, 17                        */
    0x004e,     /* 18: jmp x--, 14                        */
    /* pulse counter, on SM1 at full speed: x counts down per pulse */
    0x20a0,     /* 19: wait 1 pin, 0    ; STEP high       */
    0x2020,     /* 20: wait 0 pin, 0    ; STEP low        */
    0x0053      /* 21: jmp x--, 19                        */
};
#define PROG_LEN    19              /* the stepper, SM0 */
#define COUNT_START 19              /* the counter, SM1 */
#define ALL_LEN     (sizeof(stepper_prog) / sizeof(stepper_prog[0]))
#define SLICE_TICKS 10000UL         /* 1 ms at 10 MHz */

/* ==== shared between the halves ========================================= */

struct move {
    long            steps;          /* signed: the sign is the direction */
    unsigned long   vmax;           /* steps/s */
    unsigned long   accel;          /* steps/s^2 */
};

#define NMOVES_Q    16

struct shared {
    /* normal half -> real-time half */
    struct rtx_ring     q;
    unsigned long       q_items[NMOVES_Q];
    struct move         moves[NMOVES_Q];

    /* real-time half -> normal half, written by the real-time half only */
    volatile unsigned long  moves_done;
    volatile unsigned long  steps_generated;
    volatile unsigned long  edges_counted;
    volatile unsigned long  underruns;
    volatile unsigned long  cycles;
    volatile int            busy;       /* a move is in progress */
    volatile int            fail;       /* the PIO counter stopped answering */

    /* real-time half, private */
    struct move     cur;
    long            left;           /* steps left in cur */
    unsigned long   v;              /* current speed, steps/s */
    unsigned long   frac;           /* step fractions, 1/1000 */
    int             dir;
};

static struct shared sh;

/* ==== real-time half (core 1) =========================================== */

static void rt_init(void *arg)
{
    struct shared *s = arg;
    unsigned int i;

    /* PIO0 out of reset */
    CLR(RESETS) = RESET_PIO0;
    while (!(REG(RESETS_DONE) & RESET_PIO0))
        ;

    /* STEP and DIR to PIO0, input enabled so that SM1 can count */
    REG(PAD(STEP_PIN)) = (REG(PAD(STEP_PIN)) & ~(PAD_OD | PAD_ISO)) | PAD_IE;
    REG(PAD(DIR_PIN)) = (REG(PAD(DIR_PIN)) & ~(PAD_OD | PAD_ISO)) | PAD_IE;
    REG(IO_CTRL(STEP_PIN)) = FUNC_PIO0;
    REG(IO_CTRL(DIR_PIN)) = FUNC_PIO0;

    /* state machine 0 */
    REG(PIO_CTRL) = 0;
    for (i = 0; i < ALL_LEN; i++)
        REG(PIO_INSTR_MEM(i)) = stepper_prog[i];
    REG(PIO_SM0_CLKDIV) = 15UL << 16;                   /* 150 MHz / 15 */
    REG(PIO_SM0_EXECCTRL) = ((PROG_LEN - 1) << 12) | (0 << 7);  /* wrap */
    REG(PIO_SM0_SHIFTCTRL) = (1UL << 30);               /* join TX FIFO, 8 deep,
                                                         * shift left (MSB first) */
    REG(PIO_SM0_PINCTRL) = (1UL << 26) | (1UL << 20)    /* 1 SET pin, 1 OUT pin */
                         | ((unsigned long)STEP_PIN << 5) | DIR_PIN;
    /* both pins outputs, low: set pindirs, 1 / set pins, 0 on each */
    REG(PIO_SM0_PINCTRL) = (1UL << 26) | ((unsigned long)STEP_PIN << 5);
    REG(PIO_SM0_INSTR) = 0xe081;                        /* set pindirs, 1 */
    REG(PIO_SM0_INSTR) = 0xe000;                        /* set pins, 0 */
    REG(PIO_SM0_PINCTRL) = (1UL << 26) | ((unsigned long)DIR_PIN << 5);
    REG(PIO_SM0_INSTR) = 0xe081;
    REG(PIO_SM0_INSTR) = 0xe000;
    REG(PIO_SM0_PINCTRL) = (1UL << 26) | (1UL << 20)
                         | ((unsigned long)STEP_PIN << 5) | DIR_PIN;
    REG(PIO_SM0_INSTR) = 0x0000;                        /* jmp 0 */

    /* state machine 1: counts pulses on the STEP pin */
    REG(PIO_SM1_CLKDIV) = 1UL << 16;                    /* full speed */
    REG(PIO_SM1_EXECCTRL) = ((ALL_LEN - 1UL) << 12) | ((unsigned long)COUNT_START << 7);
    REG(PIO_SM1_SHIFTCTRL) = 0;
    REG(PIO_SM1_PINCTRL) = (unsigned long)STEP_PIN << 15;   /* IN_BASE */
    REG(PIO_SM1_INSTR) = 0xa02b;                        /* mov x, ~null */
    REG(PIO_SM1_INSTR) = COUNT_START;                   /* jmp COUNT_START */

    REG(PIO_FDEBUG) = FDEBUG_TXSTALL0;
    REG(PIO_CTRL) = 3;                                  /* enable SM0, SM1 */
    (void)s;
}

/* pulses counted by SM1 so far: it counts x down from ~0 */
static unsigned long pulses_counted(struct shared *s)
{
    unsigned int tries = 1000;

    REG(PIO_SM1_INSTR) = 0xa0c1;                        /* mov isr, x */
    REG(PIO_SM1_INSTR) = 0x8000;                        /* push noblock */
    while (REG(PIO_FSTAT) & FSTAT_RXEMPTY1)
        if (!--tries)
        {
            s->fail = 1;
            return s->edges_counted;
        }
    return ~REG(PIO_RXF1);
}

/* next 1 ms slice of the current move, as a PIO word */
static unsigned long next_slice(struct shared *s)
{
    struct move *m = &s->cur;
    unsigned long dv = m->accel / 1000;         /* speed change per ms */
    unsigned long brake, n, d;

    if (dv == 0)
        dv = 1;

    /* brake when the distance needed to stop at this speed runs out */
    brake = (unsigned long)(((unsigned long long)s->v * s->v) / (2 * m->accel));
    if ((unsigned long)s->left <= brake)
        s->v = (s->v > dv + 200) ? s->v - dv : 200;
    else if (s->v < m->vmax)
        s->v = (s->v + dv > m->vmax) ? m->vmax : s->v + dv;

    s->frac += s->v;                            /* steps * 1000 in 1 ms */
    n = s->frac / 1000;
    s->frac %= 1000;
    if (n > (unsigned long)s->left)
        n = s->left;
    if (n > 1400)
        n = 1400;

    if (n == 0)                                 /* pause for 1 ms */
        return ((unsigned long)s->dir << 30) | ((SLICE_TICKS - 5) / 2);

    s->left -= n;
    s->steps_generated += n;
    d = (SLICE_TICKS / n - 7) / 2;
    return 0x80000000UL | ((unsigned long)s->dir << 30) | ((n - 1) << 16) | d;
}

static void rt_cycle(void *arg)
{
    struct shared *s = arg;
    int started = 0;

    s->cycles++;

    /* the pulses that really appeared on the STEP pin */
    s->edges_counted = pulses_counted(s);

    /* the FIFO ran dry while a move was in progress: an underrun */
    if (REG(PIO_FDEBUG) & FDEBUG_TXSTALL0)
    {
        REG(PIO_FDEBUG) = FDEBUG_TXSTALL0;
        if (s->busy)
            s->underruns++;
    }

    /* keep the FIFO full, a slice at a time */
    while (!(REG(PIO_FSTAT) & FSTAT_TXFULL0))
    {
        if (s->left == 0)
        {
            unsigned long idx;

            if (s->busy)
            {
                s->busy = 0;
                s->moves_done++;
            }
            if (!rtx_ring_get(&s->q, &idx))
                break;                          /* nothing to do */
            s->cur = s->moves[idx];
            s->dir = s->cur.steps < 0;
            s->left = s->dir ? -s->cur.steps : s->cur.steps;
            s->v = 200;
            s->frac = 0;
            if (!s->busy)
            {
                started = 1;
                s->busy = 1;
            }
        }
        REG(PIO_TXF0) = next_slice(s);
    }

    /* The FIFO was empty while idle, which is no underrun.  The flag can
     * only be cleared once the state machine has data again: while it
     * waits on the empty FIFO, the PIO sets it right back. */
    if (started)
        REG(PIO_FDEBUG) = FDEBUG_TXSTALL0;
}

/* ==== normal half (core 0) ============================================== */

static const struct move demo_moves[] = {
    {   4000,  8000,  40000 },
    {  -4000,  8000,  40000 },
    {  20000, 40000, 100000 },
    { -20000, 40000, 100000 },
    {   1000,  2000,  20000 },
    {  -1000,  2000,  20000 },
    {  50000, 80000, 200000 },
    { -50000, 80000, 200000 },
    {    200,  1000,  10000 },
    {   -200,  1000,  10000 },
    {  30000, 60000, 150000 },
    { -30000, 60000, 150000 },
};
#define NDEMO   (sizeof(demo_moves) / sizeof(demo_moves[0]))

#define LOADFILE    "C:\\LOAD.DAT"
#define LOADSIZE    (256L * 1024)

static char buf[8192];

static unsigned long hz200(void)
{
    return (unsigned long)Ssystem(S_GETLVAL, 0x4ba, 0);    /* _hz_200 */
}

/* a file to keep the SD card busy with */
static int make_loadfile(void)
{
    long fh, n;

    fh = Fopen(LOADFILE, 0);
    if (fh >= 0)
    {
        Fclose((short)fh);
        return 1;
    }
    printf("Creating %s (%ld KB) ...\r\n", LOADFILE, LOADSIZE / 1024);
    fh = Fcreate(LOADFILE, 0);
    if (fh < 0)
        return 0;
    memset(buf, 0x5a, sizeof(buf));
    for (n = 0; n < LOADSIZE; n += sizeof(buf))
        if (Fwrite((short)fh, sizeof(buf), buf) != (long)sizeof(buf))
        {
            Fclose((short)fh);
            return 0;
        }
    Fclose((short)fh);
    return 1;
}

int main(void)
{
    struct rtx_api *rtx;
    struct rtx_status st;
    unsigned long total = 0, sd_bytes = 0, next_print, t_start, secs;
    unsigned long max_late = 0;
    long task, fh = -1, n;
    unsigned int queued = 0, i;
    int ok;

    printf("\r\npTOS real-time demonstrator: STEP GPIO%d, DIR GPIO%d\r\n",
           STEP_PIN, DIR_PIN);

    if (Ssystem(S_GETCOOKIE, RTX_COOKIE, (long)&rtx) != 0 || !rtx)
    {
        printf("No real-time core (_RTX cookie missing).\r\n");
        return 1;
    }
    if (!make_loadfile())
        printf("Cannot create %s, running without SD load.\r\n", LOADFILE);

    sh.q.size = NMOVES_Q;
    sh.q.items = sh.q_items;
    for (i = 0; i < NDEMO; i++)
        total += demo_moves[i].steps < 0 ? -demo_moves[i].steps : demo_moves[i].steps;

    task = rtx->start(rt_init, rt_cycle, &sh, 1000);
    if (task < 0)
    {
        printf("rtx start failed: %ld\r\n", task);
        return 1;
    }
    rtx->status(&st);                           /* reset the statistics */
    printf("Real-time task %ld started, %u moves, %lu steps.\r\n\r\n",
           task, (unsigned int)NDEMO, total);

    t_start = hz200();
    next_print = t_start + 200;
    while (sh.moves_done < NDEMO || sh.busy || rtx_ring_count(&sh.q))
    {
        /* feed moves as the queue has room */
        while (queued < NDEMO && rtx_ring_count(&sh.q) < NMOVES_Q)
        {
            sh.moves[queued % NMOVES_Q] = demo_moves[queued];
            rtx_ring_put(&sh.q, queued % NMOVES_Q);
            queued++;
        }

        /* keep the system core busy: read the SD card flat out */
        if (fh < 0)
            fh = Fopen(LOADFILE, 0);
        if (fh >= 0)
        {
            n = Fread((short)fh, sizeof(buf), buf);
            if (n > 0)
                sd_bytes += n;
            else
            {
                Fclose((short)fh);
                fh = -1;
            }
        }

        if (hz200() >= next_print)
        {
            next_print += 200;
            rtx->status(&st);
            if (st.max_late_us > max_late)
                max_late = st.max_late_us;
            printf("moves %2lu/%u  steps %6lu/%lu  edges %6lu  underruns %lu  "
                   "SD %5lu KB  late %3lu us\r\n",
                   sh.moves_done, (unsigned int)NDEMO, sh.steps_generated, total,
                   sh.edges_counted, sh.underruns, sd_bytes / 1024,
                   st.max_late_us);
        }

        if (Cconis())
        {
            Cnecin();
            printf("Stopped by key.\r\n");
            break;
        }
    }
    if (fh >= 0)
        Fclose((short)fh);

    /* the last edges are counted in the following cycle */
    t_start = hz200() - t_start;
    n = hz200();
    while (hz200() < (unsigned long)n + 4)
        ;
    rtx->stop(task);
    rtx->status(&st);
    if (st.max_late_us > max_late)
        max_late = st.max_late_us;
    secs = t_start / 200;

    ok = sh.steps_generated == total && sh.edges_counted == total && sh.underruns == 0;
    printf("\r\n==== result after %lu s ====\r\n", secs);
    printf("steps commanded %lu, generated %lu, counted on the pin %lu\r\n",
           total, sh.steps_generated, sh.edges_counted);
    printf("FIFO underruns during moves: %lu\r\n", sh.underruns);
    printf("real-time cycles: %lu, worst lateness %lu us\r\n", sh.cycles, max_late);
    printf("system core meanwhile read %lu KB from SD\r\n", sd_bytes / 1024);
    printf("%s\r\n", ok ? "PASS" : "FAIL");

    return ok ? 0 : 1;
}
