/*
 * kapi.c - the kernel's call table, what pTOS calls on core 0
 *
 * struct kernel_api (rtx_rp2350.h) is the whole interface: pTOS finds it
 * through struct rtx_image and calls into it like into a library, in its
 * own context.  Everything here is IRKernel's; this only translates.
 */

#include <stdint.h>
#include "IRKernel.h"
#include "rtx_rp2350.h"
#include "kapi.h"

static void (*core0_idle)(void);

/* irk_port_idle() on core 0 */
void kapi_idle(void)
{
    if (core0_idle)
        core0_idle();
}

/* pTOS becomes core 0's main task, with the share every task has at
   first.  The timer runs by now: pTOS set it up before calling. */
static long start_core0(void)
{
    return irk_init(1) == 0 ? 0 : -1;
}

static unsigned long task_create(void (*entry)(void), void *stack,
                                 unsigned long size, unsigned short prio)
{
    return irk_task_create(entry, prio, stack, size, NULL);
}

static long task_kill(unsigned long task)
{
    return irk_task_kill((irk_task_t)task);
}

static void block(void)
{
    irk_task_suspend(irk_task_self());
}

static void wake(unsigned long task)
{
    irk_task_resume((irk_task_t)task);
}

static unsigned long self(void)
{
    return irk_task_self();
}

static long set_prio(unsigned long task, unsigned short prio)
{
    return irk_task_set_prio((irk_task_t)task, prio);
}

static void set_idle(void (*fn)(void))
{
    core0_idle = fn;
}

const struct kernel_api kernel_api = {
    3,
    sizeof(struct kernel_api),
    kapi_start_core1,
    start_core0,
    task_create,
    task_kill,
    irk_yield,
    block,
    wake,
    self,
    set_prio,
    set_idle
};
