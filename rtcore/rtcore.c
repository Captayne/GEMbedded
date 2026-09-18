/*
 * rtcore.c - pTOS real-time core runtime (RP2350, core 1)
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * Runs on core 1 of the RP2350, next to pTOS on core 0.  pTOS starts it
 * at boot (see include/rtx_rp2350.h in pTOS) and forwards the rtx_api
 * calls of programs through a mailbox in SRAM.  Each real-time task a
 * program starts becomes a cyclic IRKernel task that calls the program's
 * function once per period.
 *
 * The runtime's own main task polls the mailbox.  It is an ordinary,
 * non-cyclic IRKernel task, so cyclic tasks always take precedence over
 * it and a slow command never delays a real-time call.
 */

#include <stdint.h>
#include <stddef.h>
#include "IRKernel.h"
#include "rtx.h"
#include "rtx_rp2350.h"
#include "rtcore.h"

#define RTCORE_VERSION  1
#define MAX_RT          6
#define RT_STACK        2048
#define POLL_US         200         /* mailbox poll interval */

#define mailbox ((struct rtx_mailbox *)RTX_MAILBOX_ADDR)

struct rt_slot {
    volatile int    used;
    volatile int    stop;
    irk_task_t      task;
    rtx_func        init;
    rtx_func        cyclic;
    void           *arg;
    uint32_t        period_us;
    irk_time_t      t0;             /* start of the activation grid */
    uint64_t        stack[RT_STACK / 8];
};

static struct rt_slot slots[MAX_RT];

/* statistics, reset by every STATUS command */
static volatile uint32_t max_late_us;
static volatile uint32_t calls;
static volatile long last_error;

void rtcore_error(int code, int detail)
{
    (void)detail;
    last_error = code;
}

/* IRKernel needs these two; there is no C library */
void *memset(void *s, int c, size_t n)
{
    uint8_t *p = s;

    while (n--)
        *p++ = (uint8_t)c;
    return s;
}

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dp = d;
    const uint8_t *sp = s;

    while (n--)
        *dp++ = *sp++;
    return d;
}

static struct rt_slot *self_slot(void)
{
    irk_task_t me = irk_task_self();
    int i;

    for (i = 0; i < MAX_RT; i++)
        if (slots[i].used && slots[i].task == me)
            return &slots[i];
    return NULL;
}

/* body of every real-time task */
static void rt_task(void)
{
    struct rt_slot *s = self_slot();
    uint32_t late;

    if (!s)
        return;

    if (s->init)
        s->init(s->arg);

    while (!s->stop)
    {
        /* lateness of this activation against its slot in the grid */
        late = (uint32_t)((irk_now_us() - s->t0) % s->period_us);
        if (late > max_late_us)
            max_late_us = late;

        s->cyclic(s->arg);
        calls++;

        irk_yield();                /* done for this period */
    }

    s->used = 0;                    /* returning deletes the task */
}

static long cmd_start(rtx_func init, rtx_func cyclic, void *arg, uint32_t period_us)
{
    struct rt_slot *s = NULL;
    int i;

    if (!cyclic || period_us < 100)
        return RTX_E_BADARG;

    for (i = 0; i < MAX_RT; i++)
        if (!slots[i].used)
        {
            s = &slots[i];
            break;
        }
    if (!s)
        return RTX_E_NOSLOT;

    s->init = init;
    s->cyclic = cyclic;
    s->arg = arg;
    s->period_us = period_us;
    s->stop = 0;
    s->used = 1;

    /* the new task cannot run before this one yields, so its slot is
     * complete by the time rt_task() looks it up */
    s->task = irk_task_create(rt_task, 100, s->stack, sizeof(s->stack), "rt");
    if (s->task == IRK_NO_TASK)
    {
        s->used = 0;
        return RTX_E_NOSLOT;
    }
    /* IRKernel activates it at  now + k * period  (never earlier) */
    s->t0 = irk_now_us();
    irk_task_set_cyclic_us(s->task, period_us);

    return i;
}

static long cmd_stop(long task)
{
    struct rt_slot *s;

    if (task < 0 || task >= MAX_RT || !slots[task].used)
        return RTX_E_BADARG;

    s = &slots[task];
    s->stop = 1;
    /* it ends at its next activation; wait for that, so that the caller
     * can rely on the function not being called any more */
    while (s->used)
        irk_delay_us(s->period_us);

    return 0;
}

static long cmd_status(void)
{
    int i, n = 0;

    for (i = 0; i < MAX_RT; i++)
        if (slots[i].used)
            n++;

    mailbox->status[0] = RTCORE_VERSION;
    mailbox->status[1] = n;
    mailbox->status[2] = (uint32_t)irk_now_us();
    mailbox->status[3] = max_late_us;
    mailbox->status[4] = calls;
    mailbox->status[5] = last_error;
    max_late_us = 0;
    calls = 0;

    return 0;
}

static void do_command(void)
{
    uint32_t seq = mailbox->seq;
    long rc;

    __asm__ volatile ("dmb" ::: "memory");     /* seq before command */

    switch (mailbox->cmd)
    {
    case RTX_CMD_START:
        rc = cmd_start((rtx_func)mailbox->args[0], (rtx_func)mailbox->args[1],
                       (void *)mailbox->args[2], mailbox->args[3]);
        break;
    case RTX_CMD_STOP:
        rc = cmd_stop((long)mailbox->args[0]);
        break;
    case RTX_CMD_STATUS:
        rc = cmd_status();
        break;
    default:
        rc = RTX_E_BADARG;
        break;
    }

    mailbox->result = rc;
    __asm__ volatile ("dmb" ::: "memory");     /* result before done */
    mailbox->done = seq;
}

int main(void)
{
    irk_init(1);

    mailbox->version = RTCORE_VERSION;
    mailbox->seq = 0;
    mailbox->done = 0;
    __asm__ volatile ("dmb" ::: "memory");
    mailbox->magic = RTX_MAILBOX_MAGIC;         /* pTOS waits for this */

    for (;;)
    {
        if (mailbox->seq != mailbox->done)
            do_command();
        irk_delay_us(POLL_US);
    }
}
