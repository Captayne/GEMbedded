# Multitasking

What it would take for starting a program to mean **starting a task**, and
why the scheduler is the easy part.

TOS has no threads, no semaphores, no queues and no cyclic tasks.  Giving
it those is not "pTOS with a different scheduler" -- it is the reason
somebody would choose GEMbedded over EmuTOS.  This note records what
stands in the way, and in which order to remove it.

See [scheduler-abi.md](scheduler-abi.md) for the seam this builds on.


## A program today is a jump, not a task

`proc_go()` (`bdos/proc.c:605`) already builds a complete per-program
context on ARMv8-M:

```c
sp->control = 1;                /* user mode, interrupts enabled */
sp->exc_return = 0xfffffffdUL;  /* Thread mode, process stack */
sp->frame_regs[6] = (ULONG)p->p_tbase & ~1UL;   /* entry point */
p->p_areg[7-3] = (long) sp;     /* saved stack pointer, in the basepage */
run = (PD *)p;
```

Own stack, unprivileged on the PSP, entry point, and the saved stack
pointer kept **in the basepage**.  A PD is already most of a task control
block.  What is missing is not the context -- it is that only one of them
can be current.  Starting is `run = p; gouser()`, ending is
`run = run->p_parent` (`bdos/proc.c:705`): strictly nested, one at a time.


## The one thing that has to change

`AESPD` (`aes/struct.h:120`) holds `p_link`, `p_uda`, `p_cda`, event
blocks and a message queue -- and **no pointer to a GEMDOS PD**.  No AES
code touches `run` either.

So the AES switches register contexts (the UDA) while GEMDOS' idea of the
current process stays where it is.  The desktop and the accessories share
one current directory, one file handle table, one memory owner.  That is
sufficient for what runs today and breaks the moment two programs each
want their own.

**`run` has to follow the task switch.**  That is the real work.  The
scheduler, as [scheduler-abi.md](scheduler-abi.md) shows, is twenty
lines.


## Why a cooperative kernel is what makes this possible

GEMDOS is not reentrant.  Two tasks inside it at once would take it
apart.  That this is not a problem today rests entirely on the AES
switching **cooperatively**: a GEMDOS call is atomic because nothing
gives way in the middle of it.

IRKernel has the same property -- purely cooperative, switching only at
`irk_yield()`.  A preemptive kernel would force a lock around the whole
of GEMDOS, and the idea would be dead.  So it becomes a rule:

> **Nothing yields inside GEMDOS.**  A call that has to wait yields only
> at defined points, with its state consistent, or it does not yield at
> all.

The same rule already governs rtcore on core 1, from the other side: real
-time code must not call GEMDOS, BIOS, XBIOS, VDI or AES.


## Two kinds of task

| Kind | Needs | Cost |
|---|---|---|
| plain kernel task | a stack and an entry point.  No AES, no GEMDOS -- the rtcore rule | small |
| full GEM application | an AESPD, an event queue, a share of the menu bar | large |

Build the first kind first.  It is cheap, immediately useful (workers,
the real-time half, background jobs), and it forces `run` to be carried
correctly -- which is the groundwork for the second.

The second kind runs into three known obstacles, all of them older than
this project:

- `NUM_PDS` is `NUM_ACCS + 2` (`aes/struct.h:36`) -- accessories, the
  control process and *one* DOS application.
- `pd_index()` (`aes/gempd.c:32`) maps a number to a process descriptor
  by fixed arithmetic.
- `gl_mntree` is a single global menu tree (`aes/gemctrl.c:334`): one menu
  bar for the system, not one per application.


## Program shape

A program is one GEM task on core 0.  That does not change, and a
program that never asks for anything else behaves exactly as it does
today -- no extra stack, no extra task, no cost.

If it wants a second half, **it asks for one itself**: its GEM half
creates a *headless* task on core 1.  Headless means what rtcore means
today -- no AES, no VDI, no GEMDOS, no console.  It computes, it drives
hardware, it keeps deadlines.  Creation is explicit; ownership is not.

**Ownership is not optional.**  The headless task's stack lives in the
program's own memory, and GEMDOS frees that memory when the program
ends.  So `Pterm` kills the program's core-1 tasks *before* releasing the
TPA.  Without that, a task keeps running on a freed stack -- the same
failure that took the IRKernel host port apart when a context was reused
after being released, only one storey up.

### Talking back to the UI

A headless task cannot draw: it has no operating system.  What it has is
shared memory -- both halves see the program's own data, because it *is*
the same program -- and one notification path:

```
core 1: write result to shared memory, raise a flag
core 0: forkq()  ->  AES message  ->  evnt_multi(MU_MESAG) returns
```

That is not a new mechanism on the application's side.  The GEM half is
already sitting in `evnt_multi()` waiting for messages; this is one more
sender.  `forkq()` exists for exactly this shape of problem: something
happened asynchronously, deliver it at the next safe point.

Two limits to respect.  The fork ring is bounded (`NFORKS`) and
`forkq()` returns -1 when it is full, so a task must **coalesce**: one
pending flag per program, not one message per event.  And when core 0 is
in `stop_until_interrupt()`, nothing from core 1 wakes it -- the
millisecond timer does, which bounds the latency at about 1 ms.  That is
ample for a user interface.  The RP2350's SIO FIFO has a doorbell
interrupt on the opposite core if that is ever not enough; it is not
worth building before the latency is measured and found wanting.

### Do not spin

`for (;;) yield();` reads like the `IRKernel_loops.h` model, but on a
real-time core an always-runnable task takes its share of the processor
and that share comes off the cyclic tasks' margin.  On Arduino there is
nothing else to do; here there is.  A headless task **blocks** -- on a
queue or a semaphore -- or it is cyclic with a declared period.

### Threads on core 0, later

"Extra tasks only on core 1" is a consequence, not a rule: a core-1 task
cannot touch the operating system, and a second *process* on core 0 would
need its own GEMDOS context.

But a second task on core 0 that **shares its creator's `run`** is a
thread, not a process: same file handles, same current directory, same
memory owner.  `run` does not change across that switch at all.  That is
the cheap way to do background work that needs GEMDOS -- loading a file,
say, which is impossible on core 1 by definition.  Worth keeping the door
open for; not worth building first.


## Pexec: a new mode, mode 0 unchanged

`Pexec(0, ...)` must stay synchronous.  Every TOS program relies on it
returning only when the child is finished, and a program that gets
control back early will free memory the child is still using.

A new mode beside it -- start as a task, return at once -- is the
compatible way; MiNT extended the mode range for its own purposes in the
same fashion.

The first visible gain is already waiting: `examples/deploy` calls
`Pexec(0, ...)` and is dead for as long as the deployed program runs.
With the new mode the deploy terminal stays alive, and a second deploy
can follow while the first program is still running.  Small demonstration,
large consequence.


## Where the calls live

Three layers, and they are not stacked on each other -- the first and
second both sit directly on the kernel:

| Layer | Who calls it | Size |
|---|---|---|
| `include/sched_abi.h` | pTOS internally | 9 calls, never grows |
| `_IRK` (`irk.h`) | applications that want the kernel | the full set, both cores |
| `_RTX` (`rtx.h`) | applications that want it simple | one cyclic function |

`_RTX` is to `_IRK` what `IRKernel_loops.h` is to `IRKernel.h`: a
convention instead of an API.  Keep functionality, hide complexity -- by
layering, not by leaving things out.

Both application interfaces are found through the cookie jar, not linked:
a program asks `Ssystem(S_GETCOOKIE, ...)` and gets a struct of function
pointers whose header is MIT.  Absent cookie, absent feature -- which is
exactly what the mechanism is for.

One constraint on core 0: GEM processes run **unprivileged** (`gotopgm`
sets `CONTROL |= 3`), and on ARMv8-M `CPS` is silently ignored in
unprivileged mode.  A kernel call that needs a critical section and is
reached as a plain function pointer would run unprotected and never know.
So `_IRK` goes through `SVC` on core 0, like the TOS traps.  Plain
pointers are for calls that only read.  Worth checking whether `_TCH` and
`_UCN` are affected; they work today, but possibly by luck.

`lock`/`unlock` stays out of the application interface altogether.  A
program that locks the scheduler and then loops stops the real-time half
on core 1 and the whole desktop on core 0.


## What the licence does and does not gate

Nothing here is a feature switch.  IRKernel is free for private,
educational and research use; payment applies to selling devices.  So
every hobbyist enables it, the SDK may treat `_IRK` as commonly present,
and only a vendor shipping a product pays.

That matters for the design: there is no second class of application to
design around, and no reason to keep the interesting calls out of the
SDK.
