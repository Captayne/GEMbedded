# Scheduler ABI

A seam between the AES and whatever schedules it, so that GEMbedded can
run on the dispatcher pTOS has today *or* on IRKernel, chosen at build
time.

The motive is engineering, not licensing.  The default backend stays the
known-good AES dispatcher, which keeps the fork anchored to upstream pTOS
and gives an A/B reference to measure against.  What the seam is **not**
is a licence boundary -- see the last section.


## What the AES scheduler actually is

Less than it looks like.  `insert_process()` (`aes/gempd.c:134`) walks to
the end of the list and appends:

```c
/* find the end */
for (p = (q = (AESPD *)root)->p_link; p; p = (q = p)->p_link)
    ;
```

So `rlr` is a plain FIFO.  `disp()` (`aes/gemdisp.c:221`) takes the head,
puts it back at the tail, and switches.  No priorities, no time slices,
no accounting -- round robin in about twenty lines.

The substance of `gemdisp.c` is elsewhere, and none of it is scheduling:

| Piece | Where | What it does |
|---|---|---|
| fork ring | `forkq()` `:52`, `forker()` `:101` | deferred interrupt work, run in scheduler context |
| keyboard poll | `chkkbd()` `:158` | pulls keys into the owner's queue |
| idle | `schedule()` `:181` | spins until `rlr` or `fpcnt`, else stops the CPU |
| wait/wake | `disp_act()` `:74`, `mwait_act()` `:82` | `rlr` <-> `nrl` by event flags |

Two consequences.  Replacing the policy with IRKernel is an **upgrade**
(priorities, proportional fair share, cyclic tasks with deadlines) rather
than a risk, because there is barely any policy to preserve.  And the
hard part of the port is not the scheduler at all -- it is the fork ring
and the idle path, which have no counterpart in a normal kernel.


## The calls

Nine, not five.  The last four fall out of `disp()` and are the reason a
`yield/block/wake` triple alone does not carry the AES.

```c
typedef uint32_t k_task_t;

k_task_t k_task_create(void (*entry)(void *), void *arg,
                       void *stack, size_t size, unsigned prio);
void     k_task_exit(void);              /* never returns */
void     k_yield(void);                  /* caller stays runnable */
void     k_block(unsigned reason);       /* caller waits until woken */
void     k_wake(k_task_t t);             /* from a task */
int      k_wake_isr(k_task_t t);         /* from an interrupt, may defer */
k_task_t k_current(void);
void     k_set_idle(void (*fn)(void));     /* nothing runnable */
void     k_set_prepare(void (*fn)(void));  /* before each selection */
```

`k_set_idle` is where `chkkbd()` and `stop_until_interrupt()` go: without
it a system whose processes all wait would stop taking keys.  IRKernel
already has the matching place in `irk_port_idle()`.

`k_set_prepare` is where `forker()` goes.  The fork functions are not
tasks; `forker()` deliberately sets `rlr = (AESPD *)-1` so that anything
assuming a current process crashes loudly.  A backend must therefore run
the hook **between** tasks, not inside one.

`k_wake_isr` is what `forkq()` is today.  IRKernel rejects most calls from
an interrupt (`IRK_ERR_IN_ISR`) but allows some -- reprioritising, taking
effect at the next yield -- so the deferred shape is already provided for.

`prio` is ignored by the AES backend.  It exists because an ABI that
cannot express priority is not worth having outside pTOS.

Stacks are supplied by the caller, matching IRKernel's own contract and
the AVR and Cortex-M ports: on an MCU there may be no `malloc`, and the
stack often has to sit in a particular region -- rtcore's does.


## Mapping

| AES | ABI | Note |
|---|---|---|
| `AESPD *` | `k_task_t` | id kept in the UDA |
| `rlr` | runnable set | head is `k_current()` |
| `nrl` | blocked | `k_block(WAITIN)` |
| `drl` | wake queue | `k_wake()` from a fork function |
| `dsptch()` | `k_yield()` / `k_block()` | which one depends on `p_stat & WAITIN` |
| `switchto()` | inside the backend | never returns |
| `forkq()` | `k_wake_isr()` | plus the prepare hook |
| `indisp` | adapter-local | not the backend's business |

Three constraints the adapter has to honour:

`dsptch()` must not nest, and `switchto()` never returns -- that is what
`indisp` guards (`aes/arch/armv8m/gemasm.S:89`).  It stays on the pTOS
side of the seam.

`evnt_*` timeouts need nothing from the ABI at first.  They ride on
`tchange` forks driven by the timer tick, so block/wake covers them.

The AES creates its processes with their UDA and stack already in place.
`k_task_create()` adopts that memory rather than allocating.


## Two backends

| | `sched_aes.c` | `sched_irk.c` |
|---|---|---|
| policy | FIFO round robin, as today | fair share, priorities, cyclic |
| context switch | `gemasm.S` | `irk_ctx_switch()` |
| behaviour | unchanged by construction | equal priorities approximate today |
| status | default | opt-in |

A Kconfig choice under `aes/Kconfig` selects one.  Keeping the AES
backend as the default is not caution for its own sake: it is the
reference the IRKernel backend gets measured against, the same way
running rtcore from SRAM was measured (worst-case lateness 25 us -> 8 us).


## What this is not

It is not a licence boundary, and it should not be presented as one.  The
result is a single firmware image, one address space, one linker script,
one timer tick; an `SVC` within that image is not a process boundary.  The
FSF's "arm's length" reasoning is about separate *programs*, and an
interface visibly built to evade the GPL argues against itself.

The project already has a separation that does hold: **rtcore on core 1**
-- its own image at its own flash address, loaded at runtime, talking only
through the MIT-licensed `_RTX` cookie interface.

The way to put IRKernel inside the AES without contortions is option A in
[licensing.md](licensing.md): dual-licence it, GPL arm for GEMbedded,
commercial arm for everyone else.  The seam is then free to sit where it
belongs technically instead of where a licence pushes it.


## Open questions

- Does anything in the AES rely on strict round robin?  The screen
  manager and `gl_mowner` are the places to check first.
- IRKernel's deadlock detection (`IRK_DEADLOCK_CYCLES`) versus an AES that
  is legitimately idle with every process waiting.
- `NUM_PDS`, the hardcoded `pd_index()` and `gl_mntree` as a single global
  menu tree all block real multi-app, independently of this seam.


## Steps

1. **A** -- introduce the seam, implement `sched_aes.c`, change nothing
   observable.  Verifiable: the desktop behaves exactly as before.
2. **B** -- `sched_irk.c`, measured against A.
3. **C** -- multi-app and cyclic tasks exposed to GEM programs in the SDK.
