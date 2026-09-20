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
#include "irk.h"
#include "rtx_rp2350.h"
#include "rtcore.h"

#define RTCORE_VERSION  1
#define MAX_RT          6
#define RT_STACK        2048
#define MAX_Q           6           /* queues a program may have open */

/* what a slot is being used for */
#define SLOT_FREE       0
#define SLOT_CYCLIC     1           /* _RTX: init + cyclic + period     */
#define SLOT_PLAIN      2           /* _IRK: entry(arg), waits as it likes */

/*
 * Handles carry their kind in the upper bits, so that passing a queue
 * where a semaphore belongs is refused instead of quietly corrupting
 * something.  The low bits are the index, one-based: 0 is IRK_NONE.
 */
#define H_TASK          0x1000u
#define H_SEMA          0x2000u
#define H_QUEUE         0x3000u
#define H_KIND(h)       ((h) & 0xF000u)
#define H_INDEX(h)      (((h) & 0x0FFFu) - 1u)
#define H_MAKE(k, i)    ((unsigned short)((k) | ((i) + 1u)))
#define POLL_US         200         /* mailbox poll interval */

#define mailbox ((struct rtx_mailbox *)RTX_MAILBOX_ADDR)

struct rt_slot {
    volatile int    used;
    volatile int    stop;
    irk_task_t      task;
    rtx_func        init;
    rtx_func        cyclic;
    irk_entry       fn;             /* SLOT_PLAIN: the task body        */
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

/*========================================================================*\
 *  The _IRK interface, as far as it reaches from the system core
 *
 *  Tasks live in the same slots the cyclic runtime uses: a slot is a
 *  stack plus the bookkeeping for one task, and which kind it is only
 *  decides which body runs on it.
 *
 *  Semaphores come out of IRKernel's fixed pool, handed out one at a
 *  time instead of being addressed by a fixed number -- two programs
 *  both reaching for semaphore 0 would otherwise be silently joined.
\*========================================================================*/

/*
 * IRKernel answers a status with -1 for "no", 0 for "there was nothing to
 * do" and 1 for "done".  The interface knows only 0 for accepted and
 * negative for refused, and a caller who asked for a state the task was
 * already in has no reason to hear about it.  Translated here, where the
 * two conventions meet, and nowhere else.
 */
static long done(int rc)
{
    return (rc < 0) ? IRK_ERR : IRK_OK;
}

static uint32_t    sema_taken;              /* bit per semaphore */
static irk_queue_t queues[MAX_Q];
static uint8_t     queue_taken[MAX_Q];

/* body of a plain task: run what the program asked for, then go */
static void plain_task(void)
{
    struct rt_slot *s = self_slot();

    if (!s)
        return;

    /* Without a period the body is the whole task: when it returns,
       the task is done.  With one it is entered again at every due time,
       which is what asking for a period means. */
    do {
        if (s->fn)
            s->fn(s->arg);
        if (!s->period_us)
            break;
        irk_yield();                /* until the next due time */
    } while (!s->stop);

    s->used = SLOT_FREE;            /* returning deletes the task */
}

static struct rt_slot *task_of(unsigned long h)
{
    unsigned i;

    if (H_KIND(h) != H_TASK)
        return NULL;
    i = H_INDEX(h);
    if (i >= MAX_RT || !slots[i].used)
        return NULL;
    return &slots[i];
}

static long cmd_task_new(irk_entry fn, void *arg, unsigned long prio,
                         unsigned long stack_size)
{
    struct rt_slot *s = NULL;
    int i;

    if (!fn || prio < 1 || prio > IRK_MAX_PRIO)
        return RTX_E_BADARG;
    if (stack_size > RT_STACK)      /* the pool decides, not the caller */
        return RTX_E_BADARG;

    for (i = 0; i < MAX_RT; i++)
        if (!slots[i].used)
        {
            s = &slots[i];
            break;
        }
    if (!s)
        return RTX_E_NOSLOT;

    /* fill the slot before the task exists: plain_task() looks itself up
       through it, and it must be complete by then */
    s->fn = fn;
    s->arg = arg;
    s->init = NULL;
    s->cyclic = NULL;
    s->period_us = 0;           /* a slot is reused: nothing of the task
                                   before it may survive here */
    s->stop = 0;
    s->used = SLOT_PLAIN;

    s->task = irk_task_create(plain_task, (irk_prio_t)prio,
                              s->stack, sizeof(s->stack), "app");
    if (s->task == IRK_NO_TASK)
    {
        s->used = SLOT_FREE;
        return RTX_E_NOSLOT;
    }

    /* Suspended, so that the program can finish arranging things before
       the task looks at them -- and so that a period can be set on a
       body that would otherwise have run and ended already. */
    irk_task_suspend(s->task);
    return (long)H_MAKE(H_TASK, (unsigned)i);
}

static long cmd_task_kill(unsigned long h)
{
    struct rt_slot *s = task_of(h);
    int rc;

    if (!s)
        return RTX_E_BADARG;

    rc = irk_task_kill(s->task);
    s->used = SLOT_FREE;
    return done(rc);
}

static long cmd_task_ctl(unsigned long h, unsigned long what, unsigned long v)
{
    struct rt_slot *s = task_of(h);

    if (!s)
        return RTX_E_BADARG;

    switch (what)
    {
    case RTX_CTL_SUSPEND:  return done(irk_task_suspend(s->task));
    case RTX_CTL_RESUME:   return done(irk_task_resume(s->task));
    case RTX_CTL_SET_PRIO: return done(irk_task_set_prio(s->task, (irk_prio_t)v));
    case RTX_CTL_GET_PRIO: return (long)irk_task_get_prio(s->task);
    case RTX_CTL_NORMAL:   return done(irk_task_set_normal(s->task, (irk_prio_t)v));
    case RTX_CTL_STACK:    return (long)irk_stack_free(s->task);
    case RTX_CTL_RUNTIME:  return (long)(uint32_t)irk_task_runtime_us(s->task);
    default:               return RTX_E_BADARG;
    }
}

static long cmd_task_cyclic(unsigned long h, unsigned long period_us,
                            unsigned long start_after_us)
{
    struct rt_slot *s = task_of(h);

    if (!s || period_us < 100)
        return RTX_E_BADARG;

    /* A plain task that becomes cyclic keeps its body: it is entered
       again at every due time, which is what a program asking for a
       period expects. */
    s->period_us = period_us;       /* plain_task() loops on this */
    if (start_after_us)
        return done(irk_task_set_cyclic_at_us(s->task, period_us,
                                              start_after_us));
    return done(irk_task_set_cyclic_us(s->task, period_us));
}

static long cmd_sema_new(long count)
{
    unsigned i;

    for (i = 0; i < IRK_MAX_SEMAPHORES; i++)
        if (!(sema_taken & (1UL << i)))
        {
            if (irk_sema_init((irk_sema_t)i, (int16_t)count) != 0)
                return RTX_E_BADARG;
            sema_taken |= 1UL << i;
            return (long)H_MAKE(H_SEMA, i);
        }
    return RTX_E_NOSLOT;
}

static long sema_of(unsigned long h, irk_sema_t *out)
{
    unsigned i;

    if (H_KIND(h) != H_SEMA)
        return RTX_E_BADARG;
    i = H_INDEX(h);
    if (i >= IRK_MAX_SEMAPHORES || !(sema_taken & (1UL << i)))
        return RTX_E_BADARG;
    *out = (irk_sema_t)i;
    return 0;
}

static long cmd_sema_free(unsigned long h)
{
    irk_sema_t s;
    unsigned   n;

    if (sema_of(h, &s) != 0)
        return RTX_E_BADARG;

    /* Release whoever is waiting, so that freeing cannot strand a task.
       Bounded: a task that waits again at once would otherwise spin here
       and the mailbox would stop being served -- and this loop runs on
       the very task that serves it. */
    for (n = 0; n < IRK_MAX_TASKS && irk_sema_count(s) < 0; n++)
        irk_sema_signal(s);

    sema_taken &= ~(1UL << s);
    return 0;
}

static long cmd_sema_op(unsigned long h, unsigned long op)
{
    irk_sema_t s;

    if (sema_of(h, &s) != 0)
        return RTX_E_BADARG;

    switch (op)
    {
    case RTX_SEM_SIGNAL: return done(irk_sema_signal(s));
    /* IRKernel answers 1 for "taken" and 0 for "was not free"; the
       interface answers 0 for success and negative for no.  Translate
       here rather than leave two conventions in the same call chain. */
    case RTX_SEM_TRY:    return irk_sema_try_wait(s) ? 0L : -1L;
    case RTX_SEM_COUNT:  return (long)irk_sema_count(s);
    default:             return RTX_E_BADARG;
    }
}

static long cmd_queue_new(void *storage, unsigned long items,
                          unsigned long itemsize)
{
    unsigned i;

    if (!storage || !items || !itemsize || items > 255 || itemsize > 255)
        return RTX_E_BADARG;

    for (i = 0; i < MAX_Q; i++)
        if (!queue_taken[i])
        {
            if (irk_queue_init(&queues[i], storage, (uint8_t)items,
                               (uint8_t)itemsize) != 0)
                return RTX_E_BADARG;
            queue_taken[i] = 1;
            return (long)H_MAKE(H_QUEUE, i);
        }
    return RTX_E_NOSLOT;
}

static long queue_of(unsigned long h, irk_queue_t **out)
{
    unsigned i;

    if (H_KIND(h) != H_QUEUE)
        return RTX_E_BADARG;
    i = H_INDEX(h);
    if (i >= MAX_Q || !queue_taken[i])
        return RTX_E_BADARG;
    *out = &queues[i];
    return 0;
}

static long cmd_queue_free(unsigned long h)
{
    irk_queue_t *q;

    if (queue_of(h, &q) != 0)
        return RTX_E_BADARG;

    /* Wakes tasks waiting to send; one waiting to *receive* stays where
       it is, so a program frees its queues after stopping its tasks --
       which is what ending the program does anyway. */
    irk_queue_flush(q);
    queue_taken[H_INDEX(h)] = 0;
    return 0;
}

static long cmd_queue_op(unsigned long h, unsigned long op, void *item)
{
    irk_queue_t *q;

    if (queue_of(h, &q) != 0)
        return RTX_E_BADARG;

    switch (op)
    {
    /* Same translation as for a semaphore: 1 means it happened. */
    case RTX_Q_TRY_SEND: if (!item) return RTX_E_BADARG;
                         return irk_queue_try_send(q, item) ? 0L : -1L;
    case RTX_Q_TRY_RECV: if (!item) return RTX_E_BADARG;
                         return irk_queue_try_recv(q, item) ? 0L : -1L;
    case RTX_Q_COUNT:    return (long)irk_queue_count(q);
    default:             return RTX_E_BADARG;
    }
}


/*========================================================================*\
 *  The same interface, as it exists on this core
 *
 *  What a headless task calls.  No mailbox: it is already here, and the
 *  mailbox is what this core serves.  Nothing is missing either --
 *  waiting is exactly what a task over here is allowed to do.
\*========================================================================*/

static unsigned short rt_core(void)
{
    /* As far as this kernel knows it is alone, so ask it nothing: from
       the program's side this is the real-time core, by definition. */
    return IRK_CORE_RT;
}

static unsigned short rt_cores(void)
{
    return 2;
}

static irk_handle rt_task_new(unsigned short core, irk_entry fn, void *arg,
                              unsigned short prio,
                              void *stack, unsigned long size,
                              const char *name)
{
    long rc;

    (void)stack;
    (void)name;
    if (core != IRK_CORE_RT)
        return IRK_NONE;
    rc = cmd_task_new(fn, arg, prio, size);
    return (rc > 0) ? (irk_handle)rc : IRK_NONE;
}

static long rt_task_kill(irk_handle t)     { return cmd_task_kill(t); }
static long rt_task_suspend(irk_handle t)  { return cmd_task_ctl(t, RTX_CTL_SUSPEND, 0); }
static long rt_task_resume(irk_handle t)   { return cmd_task_ctl(t, RTX_CTL_RESUME, 0); }
static long rt_set_prio(irk_handle t, unsigned short p)
                                           { return cmd_task_ctl(t, RTX_CTL_SET_PRIO, p); }
static long rt_get_prio(irk_handle t)      { return cmd_task_ctl(t, RTX_CTL_GET_PRIO, 0); }
static long rt_set_normal(irk_handle t, unsigned short p)
                                           { return cmd_task_ctl(t, RTX_CTL_NORMAL, p); }
static long rt_set_cyclic(irk_handle t, unsigned long period_us,
                          unsigned long start_after_us)
                                           { return cmd_task_cyclic(t, period_us, start_after_us); }
static long rt_stack_free(irk_handle t)    { return cmd_task_ctl(t, RTX_CTL_STACK, 0); }
static unsigned long rt_runtime_us(irk_handle t)
{
    long rc = cmd_task_ctl(t, RTX_CTL_RUNTIME, 0);
    return (rc < 0) ? 0UL : (unsigned long)rc;
}

static irk_handle rt_task_self(void)
{
    irk_task_t me = irk_task_self();
    int i;

    for (i = 0; i < MAX_RT; i++)
        if (slots[i].used && slots[i].task == me)
            return H_MAKE(H_TASK, (unsigned)i);
    return IRK_NONE;
}

static void rt_yield(void)                 { irk_yield(); }
static void rt_delay_us(unsigned long us)  { irk_delay_us(us); }
static unsigned long rt_now_us(void)       { return (unsigned long)irk_now_us(); }

static irk_handle rt_sema_new(long count)
{
    long rc = cmd_sema_new(count);
    return (rc > 0) ? (irk_handle)rc : IRK_NONE;
}
static long rt_sema_free(irk_handle s)     { return cmd_sema_free(s); }
static long rt_sema_try(irk_handle s)      { return cmd_sema_op(s, RTX_SEM_TRY); }
static long rt_sema_signal(irk_handle s)   { return cmd_sema_op(s, RTX_SEM_SIGNAL); }

static long rt_sema_wait(irk_handle h)
{
    irk_sema_t s;

    if (sema_of(h, &s) != 0)
        return RTX_E_BADARG;
    return done(irk_sema_wait(s));
}

static irk_handle rt_queue_new(void *storage, unsigned short items,
                               unsigned short itemsize)
{
    long rc = cmd_queue_new(storage, items, itemsize);
    return (rc > 0) ? (irk_handle)rc : IRK_NONE;
}
static long rt_queue_free(irk_handle q)    { return cmd_queue_free(q); }
static long rt_queue_try_send(irk_handle q, const void *i)
                                           { return cmd_queue_op(q, RTX_Q_TRY_SEND, (void *)i); }
static long rt_queue_try_recv(irk_handle q, void *i)
                                           { return cmd_queue_op(q, RTX_Q_TRY_RECV, i); }
static long rt_queue_count(irk_handle q)   { return cmd_queue_op(q, RTX_Q_COUNT, NULL); }

static long rt_queue_send(irk_handle h, const void *item)
{
    irk_queue_t *q;

    if (queue_of(h, &q) != 0)
        return RTX_E_BADARG;
    return done(irk_queue_send(q, item));
}

static long rt_queue_recv(irk_handle h, void *item)
{
    irk_queue_t *q;

    if (queue_of(h, &q) != 0)
        return RTX_E_BADARG;
    return done(irk_queue_recv(q, item));
}

static long rt_notify(unsigned long a, unsigned long b)
{
    /* Coalesce: the newest values win, and the count of notifications
       never exceeds the one slot.  The system core clears note last, so
       it never sees half of a newer pair. */
    mailbox->note_task = rt_task_self();
    mailbox->note_a = a;
    mailbox->note_b = b;
    __asm__ volatile ("dmb" ::: "memory");
    mailbox->note = 1;
    return 0;
}

static struct irk_api *rt_rt_api(void);

static const struct irk_api rt_irk_api = {
    IRK_API_VERSION,
    sizeof(struct irk_api),

    rt_core,
    rt_cores,

    rt_task_new,
    rt_task_kill,
    rt_task_suspend,
    rt_task_resume,
    rt_task_self,
    rt_set_prio,
    rt_get_prio,
    rt_set_cyclic,
    rt_set_normal,

    rt_yield,
    rt_delay_us,
    rt_now_us,

    rt_sema_new,
    rt_sema_free,
    rt_sema_wait,
    rt_sema_try,
    rt_sema_signal,

    rt_queue_new,
    rt_queue_free,
    rt_queue_send,
    rt_queue_recv,
    rt_queue_try_send,
    rt_queue_try_recv,
    rt_queue_count,

    rt_notify,
    NULL,                       /* notify_to: the GEM half says that */

    rt_stack_free,
    rt_runtime_us,

    rt_rt_api
};

static struct irk_api *rt_rt_api(void)
{
    return (struct irk_api *)&rt_irk_api;
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
    case RTX_CMD_TASK_NEW:
        rc = cmd_task_new((irk_entry)mailbox->args[0], (void *)mailbox->args[1],
                          mailbox->args[2], mailbox->args[3]);
        break;
    case RTX_CMD_TASK_KILL:
        rc = cmd_task_kill(mailbox->args[0]);
        break;
    case RTX_CMD_TASK_CTL:
        rc = cmd_task_ctl(mailbox->args[0], mailbox->args[1], mailbox->args[2]);
        break;
    case RTX_CMD_TASK_CYCLIC:
        rc = cmd_task_cyclic(mailbox->args[0], mailbox->args[1], mailbox->args[2]);
        break;
    case RTX_CMD_SEMA_NEW:
        rc = cmd_sema_new((long)mailbox->args[0]);
        break;
    case RTX_CMD_SEMA_FREE:
        rc = cmd_sema_free(mailbox->args[0]);
        break;
    case RTX_CMD_SEMA_OP:
        rc = cmd_sema_op(mailbox->args[0], mailbox->args[1]);
        break;
    case RTX_CMD_QUEUE_NEW:
        rc = cmd_queue_new((void *)mailbox->args[0], mailbox->args[1],
                           mailbox->args[2]);
        break;
    case RTX_CMD_QUEUE_FREE:
        rc = cmd_queue_free(mailbox->args[0]);
        break;
    case RTX_CMD_QUEUE_OP:
        rc = cmd_queue_op(mailbox->args[0], mailbox->args[1],
                          (void *)mailbox->args[2]);
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
    mailbox->api = (unsigned long)&rt_irk_api;
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
