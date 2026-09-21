/*
 * irktest.c - what the multitasking interface can do, tried step by step
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * A plain TOS program.  It finds the kernel through the cookie "_IRK"
 * and works through five stages, each resting on the one before:
 *
 *   1  the interface is there and the transport carries
 *   2  semaphores: handed out, used, handed back -- and a wrong handle
 *      refused rather than quietly acted on
 *   3  a headless task on the real-time core counts in shared memory
 *   4  the same on a period, and what it really costs
 *   5  a queue from the headless half up to this one
 *
 * TWO POINTERS, NOT ONE
 *
 * k  is the interface as this core sees it: every call is a request sent
 *    across, so nothing that waits is in it.
 * rt is the same interface as it exists over there, fetched with
 *    k->rt_api().  That is the one a headless task uses -- for it, the
 *    pointers in k would be a call to itself by way of the mailbox it is
 *    supposed to be serving.
 *
 * CAREFUL, and this is not a detail: a headless task runs *this
 * program's code* out of *this program's memory*.  Ending the program
 * while the task still runs leaves the real-time core executing whatever
 * lands in that memory next.  Until pTOS kills a program's tasks on
 * Pterm, the program must do it itself, on every way out.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include "irk.h"

/*
 * Just enough AES to receive a message.  libcmini has no bindings for
 * it, and a whole library would be a lot to carry for three calls: the
 * AES is one trap, and its parameter block is six pointers.
 */
static short control[5], global[16], int_in[16], int_out[16];
static long  addr_in[8], addr_out[8];
static void *aespb[6] = { control, global, int_in, int_out, addr_in, addr_out };

static short aes(short op, short nintin, short nintout, short naddrin)
{
    register long  r0 __asm__("r0") = 200;
    register void *r1 __asm__("r1") = aespb;

    control[0] = op;
    control[1] = nintin;
    control[2] = nintout;
    control[3] = naddrin;
    __asm__ volatile ("svc 2"
                      : "+r"(r0), "+r"(r1)
                      :
                      : "r2", "r3", "r12", "lr", "memory", "cc");
    return int_out[0];
}

static short appl_init(void)   { return aes(10, 0, 1, 0); }
static short appl_exit(void)   { return aes(19, 0, 1, 0); }

static short g_apid = -1;       /* this program, as the AES knows it */

/*
 * Wait for a touch, a key, or the time to run out -- whichever comes
 * first.  A program on this machine must never end by waiting for a key
 * alone: there is not always a keyboard, and then nothing but the reset
 * button gets you out.
 */
static void wait_for_a_sign(unsigned long ms)
{
    short msg[8];
    unsigned long waited;
    int   i;

    for (waited = 0; waited < ms; waited += 100)
    {
        /* Is the screen being touched right now?  graf_mkstate answers
           that to anyone; the click itself would go to the screen
           manager, since this program has no window of its own. */
        aes(79, 0, 5, 0);                   /* graf_mkstate */
        if (int_out[3] & 1)
            return;

        int_in[0] = 0x0001 | 0x0020;        /* keyboard or timer */
        for (i = 1; i < 14; i++)
            int_in[i] = 0;
        int_in[14] = 100;                   /* a tenth of a second */
        int_in[15] = 0;
        addr_in[0] = (long)msg;
        if (aes(25, 16, 7, 1) & 0x0001)     /* a key ends it too */
            return;
    }
}

/* evnt_multi, asked only for messages and a timeout */
static short evnt_mesag_timer(unsigned long ms, short *msg)
{
    int_in[0] = 0x0010 | 0x0020;        /* MU_MESAG | MU_TIMER */
    int_in[1] = int_in[2] = int_in[3] = 0;
    int_in[4] = int_in[5] = int_in[6] = int_in[7] = int_in[8] = 0;
    int_in[9] = int_in[10] = int_in[11] = int_in[12] = int_in[13] = 0;
    int_in[14] = (short)(ms & 0xffff);
    int_in[15] = (short)(ms >> 16);
    addr_in[0] = (long)msg;
    return aes(25, 16, 7, 1);
}

static struct irk_api *k;       /* here */
static struct irk_api *rt;      /* over there */

/* Shared between the halves: ordinary memory of this program.  Both
   cores see it because there is only one RAM. */
static volatile unsigned long ticks;
static volatile unsigned long stop;
static volatile unsigned long ended;

struct sample {
    unsigned long n;
    unsigned long us;
};

static struct sample q_storage[8];
static irk_handle    queue;

static int failures;

static void ok(const char *what, int good)
{
    printf("  %-44s %s\r\n", what, good ? "ok" : "FAILED");
    if (!good)
        failures++;
}

static void stage(const char *title)
{
    printf("\r\n== %s\r\n", title);
}

/* Wait without the AES: this is a TOS program, and the kernel's clock is
   right here.  Unsigned arithmetic stays exact across the 71 minute
   wrap of the microsecond counter. */
static void wait_ms(unsigned long ms)
{
    unsigned long t0 = k->now_us();

    while (k->now_us() - t0 < ms * 1000UL)
        ;
}


/*========================================================================*\
 *  The headless half.  Runs on the real-time core.
 *
 *  No GEMDOS, no BIOS, no XBIOS, no VDI, no AES: no printf, no file, no
 *  console.  It counts, it waits, it fills a queue.
\*========================================================================*/

static void counter(void *arg)
{
    unsigned long every_us = (unsigned long)arg;

    while (!stop)
    {
        ticks++;
        rt->delay_us(every_us);
    }
    ended = 1;
}

static void sampler(void *arg)
{
    struct sample s;

    (void)arg;
    s.n = 0;
    while (!stop)
    {
        s.n++;
        s.us = rt->now_us();
        rt->queue_send(queue, &s);      /* waits when it is full */
        ticks++;
    }
    ended = 1;
}

/* Reports upwards, as fast as it can: the point is that the newest
   values arrive and the AES is not flooded, not that every call lands. */
static void reporter(void *arg)
{
    unsigned long n = 0;

    (void)arg;
    while (!stop)
    {
        n++;
        rt->notify(n, rt->now_us());
        ticks++;
        rt->delay_us(200);
    }
    ended = 1;
}

/* Two of these run at once, each counting into its own place.  They
   stay runnable on purpose: only then does the share decide. */
static volatile unsigned long count_a, count_b;

static void busy(void *arg)
{
    volatile unsigned long *c = (volatile unsigned long *)arg;

    while (!stop)
    {
        (*c)++;
        rt->yield();
    }
    ended++;
}

/* A cyclic task is entered again at every due time, so it returns. */
static void ticker(void *arg)
{
    (void)arg;
    ticks++;
}


/*========================================================================*\
 *  The stages
\*========================================================================*/

static void stage_interface(void)
{
    stage("The interface is there");

    printf("  version %u, %u bytes\r\n", k->version, k->size);
    ok("the version is the one this program knows", k->version == IRK_API_VERSION);
    ok("the structure is not shorter than expected",
       k->size >= sizeof(struct irk_api));
    ok("this is the system core", k->core() == IRK_CORE_SYSTEM);
    ok("a real-time core is there", k->cores() == 2);
    ok("calls that would wait are absent here",
       k->sema_wait == 0 && k->queue_recv == 0 && k->yield == 0);

    rt = k->rt_api ? k->rt_api() : 0;
    ok("the interface over there can be reached", rt != 0);
    if (!rt)
        return;
    ok("and there nothing is missing",
       rt->sema_wait != 0 && rt->queue_recv != 0 && rt->yield != 0);
    ok("it knows which core it is on", rt->core() == IRK_CORE_RT);
}

static void stage_sema(void)
{
    irk_handle s;

    stage("Semaphores");

    s = k->sema_new(1);
    ok("one was handed out", s != IRK_NONE);
    if (s == IRK_NONE)
        return;

    ok("taking it succeeds", k->sema_try(s) == IRK_OK);
    ok("taking it again does not", k->sema_try(s) != IRK_OK);
    ok("giving it back succeeds", k->sema_signal(s) == IRK_OK);
    ok("now it can be taken again", k->sema_try(s) == IRK_OK);
    k->sema_signal(s);

    /* A handle carries its kind, so using one in the wrong place is
       refused instead of corrupting something quietly. */
    ok("a semaphore is refused as a task", k->task_kill(s) != IRK_OK);
    ok("an invented handle is refused",
       k->sema_signal((irk_handle)0x7777) != IRK_OK);

    ok("it can be handed back", k->sema_free(s) == IRK_OK);
    ok("and does not work afterwards", k->sema_signal(s) != IRK_OK);
}

static void stage_task(void)
{
    irk_handle t;
    unsigned long seen;
    int i;

    stage("A headless task on the real-time core");

    ticks = 0;
    stop = 0;
    ended = 0;

    t = k->task_new(IRK_CORE_RT, counter, (void *)1000UL, 100, 0, 1024, 0);
    ok("it was created", t != IRK_NONE);
    if (t == IRK_NONE)
        return;

    /* Born suspended, so that whatever it looks at is ready first. */
    ok("it starts when told", k->task_resume(t) == IRK_OK);

    wait_ms(200);
    seen = ticks;
    printf("  counted %lu times in about 200 ms\r\n", seen);
    ok("the other core really ran our code", seen > 20);
    ok("and is not running away", seen < 4000);

    printf("  stack never touched: %ld bytes\r\n", k->stack_free(t));
    printf("  processor had: %lu us\r\n", k->runtime_us(t));

    stop = 1;
    for (i = 0; i < 50 && !ended; i++)
        wait_ms(10);
    ok("it ended when asked", ended != 0);

    k->task_kill(t);            /* nothing of ours left running there */
}

static void stage_cyclic(void)
{
    irk_handle t;
    unsigned long seen;

    stage("The same task, on a period");

    ticks = 0;
    stop = 0;
    ended = 0;

    t = k->task_new(IRK_CORE_RT, ticker, 0, 100, 0, 1024, 0);
    ok("it was created", t != IRK_NONE);
    if (t == IRK_NONE)
        return;

    ok("it took a period of 1 ms", k->set_cyclic(t, 1000, 0) == IRK_OK);
    ok("and starts when told", k->task_resume(t) == IRK_OK);

    wait_ms(500);
    seen = ticks;
    printf("  %lu activations in about 500 ms\r\n", seen);
    ok("near a thousand, not wildly off", seen > 300 && seen < 800);
    printf("  stack never touched: %ld bytes\r\n", k->stack_free(t));

    ok("it can be killed", k->task_kill(t) == IRK_OK);
    wait_ms(50);
    seen = ticks;
    wait_ms(100);
    ok("and has really stopped", ticks == seen);
}

static void stage_two(void)
{
    irk_handle a, b;
    unsigned long ca, cb, ratio10;
    int i;

    stage("Two tasks at once, and the share divides by share");

    stop = 0;
    ended = 0;
    count_a = count_b = 0;

    /* Both created before either runs: creation does not start a task,
       so neither is greedy while the other is still being set up. */
    a = k->task_new(IRK_CORE_RT, busy, (void *)&count_a, 10, 0, 1024, 0);
    b = k->task_new(IRK_CORE_RT, busy, (void *)&count_b, 20, 0, 1024, 0);
    ok("both were created", a != IRK_NONE && b != IRK_NONE);
    ok("and they are not the same task", a != b);
    if (a == IRK_NONE || b == IRK_NONE)
        return;

    ok("the first starts when told", k->task_resume(a) == IRK_OK);
    ok("the second too", k->task_resume(b) == IRK_OK);

    /* Nothing is asked of the runtime while they run: this core only
       reads memory both cores share. */
    wait_ms(300);

    ca = count_a;
    cb = count_b;
    stop = 1;
    for (i = 0; i < 50 && ended < 2; i++)
        wait_ms(10);

    printf("  priority 10: %lu turns, priority 20: %lu turns\r\n", ca, cb);
    ok("both really ran", ca > 100 && cb > 100);

    ratio10 = ca ? (cb * 10UL) / ca : 0;
    printf("  ratio %lu.%lu to 1, expected 2.0\r\n", ratio10 / 10, ratio10 % 10);
    ok("twice the share, twice the turns", ratio10 >= 15 && ratio10 <= 25);

    printf("  tasks that ended: %lu\r\n", ended);
    ok("both ended, and neither ran on", ended == 2);
    k->task_kill(a);
    k->task_kill(b);
}



/*========================================================================*\
 *  Does a program keep its floating point registers?
\*========================================================================*/

/* Fill s16..s31, give way, and look again.  The AES has to carry them
   across the switch; nothing else does. */
static int callee_saved_survive(void)
{
    float before[16], after[16];
    short msg[8];
    int   i, same = 1;

    for (i = 0; i < 16; i++)
        before[i] = 1000.0f + (float)i;

    __asm__ volatile ("vldmia %0, {s16-s31}" : : "r"(before) : "memory");

    /* A real dispatch: this blocks, so the AES runs somebody else. */
    int_in[0] = 0x0020;                 /* MU_TIMER */
    for (i = 1; i < 14; i++)
        int_in[i] = 0;
    int_in[14] = 100;
    int_in[15] = 0;
    addr_in[0] = (long)msg;
    aes(25, 16, 7, 1);

    __asm__ volatile ("vstmia %0, {s16-s31}" : : "r"(after) : "memory");

    for (i = 0; i < 16; i++)
        if (after[i] != before[i])
            same = 0;
    return same;
}

/* Fill s0..s15 and spin long enough for a few hundred interrupts.  The
   timer runs at 200 Hz, so 100 ms is twenty of them at least. */
static int caller_saved_survive(void)
{
    float before[16], after[16];
    unsigned long t0;
    int i, same = 1;

    for (i = 0; i < 16; i++)
        before[i] = 500.0f + (float)i;

    t0 = k->now_us();

    /* Load, wait and store without letting the compiler in between: it
       may use these registers itself, and then the test would measure
       the compiler rather than the system. */
    __asm__ volatile (
        "vldmia %0, {s0-s15}\n"
        "1:\n"
        "   subs %2, %2, #1\n"
        "   bne 1b\n"
        "vstmia %1, {s0-s15}\n"
        : : "r"(before), "r"(after), "r"(2000000UL)
        : "memory", "cc");

    (void)t0;
    for (i = 0; i < 16; i++)
        if (after[i] != before[i])
            same = 0;
    return same;
}

static void stage_float(void)
{
    stage("A program keeps its floating point registers");

    ok("s16..s31 survive a process switch", callee_saved_survive());
    ok("s0..s15 survive the interrupts of 100 ms", caller_saved_survive());
}


static void stage_queue(void)
{
    irk_handle t;
    struct sample s;
    long got = 0;
    int  i;

    stage("A queue from there to here");

    ticks = 0;
    stop = 0;
    ended = 0;
    s.n = 0;

    queue = k->queue_new(q_storage, 8, sizeof(struct sample));
    ok("a queue was handed out", queue != IRK_NONE);
    if (queue == IRK_NONE)
        return;

    t = k->task_new(IRK_CORE_RT, sampler, 0, 100, 0, 1024, 0);
    ok("the sender was created", t != IRK_NONE);
    if (t == IRK_NONE)
    {
        k->queue_free(queue);
        return;
    }
    ok("the sender starts when told", k->task_resume(t) == IRK_OK);

    /* It fills the queue and then waits for room.  That is the point: it
       blocks over there without holding up anything over here. */
    for (i = 0; i < 100 && got < 20; i++)
    {
        int n;

        /* Bounded on purpose.  The sender refills as fast as this
           empties, so an unbounded drain never returns to the condition
           above -- it is the consumer's job to come up for air. */
        for (n = 0; n < 8 && k->queue_try_recv(queue, &s) == IRK_OK; n++)
        {
            if (got < 3)
                printf("  item %lu at %lu us\r\n", s.n, s.us);
            got++;
        }
        wait_ms(10);
    }
    printf("  received %ld items\r\n", got);
    ok("they came through, in order", got >= 20 && s.n == (unsigned long)got);

    stop = 1;
    for (i = 0; i < 50 && !ended; i++)
    {
        int n;

        /* Keep it moving so the sender can notice that it should end. */
        for (n = 0; n < 8 && k->queue_try_recv(queue, &s) == IRK_OK; n++)
            ;
        wait_ms(10);
    }
    ok("the sender ended", ended != 0);

    k->task_kill(t);
    ok("the queue can be handed back", k->queue_free(queue) == IRK_OK);
    queue = IRK_NONE;
}


static void stage_notify(void)
{
    irk_handle t;
    short  msg[8], ev;
    long   got = 0;
    unsigned long last = 0, sent;
    int    i;

    stage("A headless task reaches the user interface");

    ok("this program is known to the AES", g_apid >= 0);
    if (g_apid < 0)
        return;

    ok("the kernel was told where to deliver",
       k->notify_to((unsigned short)g_apid) == IRK_OK);

    ticks = 0;
    stop = 0;
    ended = 0;

    t = k->task_new(IRK_CORE_RT, reporter, 0, 100, 0, 1024, 0);
    ok("the reporting task was created", t != IRK_NONE);
    if (t == IRK_NONE)
        return;
    ok("the reporter starts when told", k->task_resume(t) == IRK_OK);

    /* Collect for about a second.  evnt_multi returns on a message or
       when the timer runs out, so this waits properly instead of
       spinning -- which is the whole point of the exercise. */
    for (i = 0; i < 40 && got < 20; i++)
    {
        ev = evnt_mesag_timer(50, msg);
        if (ev & 0x0010)                /* MU_MESAG */
        {
            if (msg[0] != IRK_MSG)
                continue;
            if (got < 3)
                printf("  message from task %d: %lu at %lu us\r\n",
                       msg[3],
                       ((unsigned long)(unsigned short)msg[4] << 16)
                           | (unsigned short)msg[5],
                       ((unsigned long)(unsigned short)msg[6] << 16)
                           | (unsigned short)msg[7]);
            last = ((unsigned long)(unsigned short)msg[4] << 16)
                   | (unsigned short)msg[5];
            got++;
        }
    }

    sent = ticks;
    printf("  sent %lu, received %ld, newest value seen %lu\r\n",
           sent, got, last);

    ok("messages arrived at all", got > 0);
    ok("they name the task that sent them", msg[3] == (short)t);
    ok("coalesced, not queued up", got <= (long)sent);
    ok("and the newest values came through", last > 0 && last <= sent);

    stop = 1;
    for (i = 0; i < 50 && !ended; i++)
        wait_ms(10);
    ok("the reporter ended", ended != 0);

    k->task_kill(t);
}


int main(void)
{
    long value = 0;

    printf("\r\nirktest: the multitasking interface, step by step\r\n");

    if (Ssystem(S_GETCOOKIE, IRK_COOKIE, (long)&value) != 0 || !value)
    {
        printf("No kernel here: the _IRK cookie is missing.\r\n");
        printf("That is an answer, not a fault -- pTOS runs without it,\r\n");
        printf("and so does every program that asks before it assumes.\r\n");
        return 1;
    }
    k = (struct irk_api *)value;
    g_apid = appl_init();

    stage_interface();
    if (!failures)
    {
        stage_sema();
        stage_task();
        stage_cyclic();
        stage_two();
        stage_float();
        stage_queue();
        stage_notify();
    }

    printf("\r\n%s\r\n", failures ? "FAILURES ABOVE" : "all stages passed");

    if (g_apid >= 0)
    {
        printf("Touch the screen or press a key. Ends by itself in 20 s.\r\n");
        wait_for_a_sign(20000);
        appl_exit();
    }
    else
    {
        printf("Press a key.\r\n");
        Cconin();
    }
    return failures ? 1 : 0;
}
