# Scheduler ABI

A seam between the AES and whatever schedules it, so that GEMbedded can
run on the dispatcher pTOS has today *or* on IRKernel, chosen at build
time.

The seam carries two things, and keeping them apart is the whole point.
Technically it keeps the known-good AES dispatcher as the default, which
anchors the fork to upstream pTOS and gives an A/B reference to measure
against.  Commercially it is the product boundary: IRKernel is a
separately licensed component that a user has to go and get, and then
select on purpose.

What it does *not* do is settle the copyright question -- whether a
combination that somebody distributes is two programs or one derived
work.  That is carried by the architecture, not by the switch.  The last
two sections say which is which.


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


## Linked or loaded

Two separate images are the strong form, but they are not the only one,
and insisting on them from day one would stall the work.  The seam makes
the coupling behind it exchangeable, so one adapter serves both:

| Mode | Coupling | Intended for |
|---|---|---|
| **1 -- AES** | none | everyone; the default |
| **2 -- IRKernel, linked** | direct calls into `libirkernel.a` | developing the scheduler, and licensees running it on their own devices |
| **3 -- IRKernel, external** | `SVC` into a separate image at its own flash address | products redistributed to third parties |

`sched_irk.c` is the same file in modes 2 and 3.  Only the binding
differs: `sched_irk_link.c` resolves the `k_*` calls directly,
`sched_irk_svc.c` through `SVC` stubs.  That is what a neutral ABI buys --
mode 3 can arrive later without GEMbedded being taken apart again.

Mode 2 is the honest development path and gets built first.  What it must
not do is carry a claim it cannot support: **a single ELF holding GPL code
and a proprietary kernel is the hard case for the "two programs"
argument, not the easy one.**  The build log says that in plain words
rather than printing a licence id and letting the reader infer the rest.


## Product boundary

IRKernel is not part of GEMbedded.  Cloning this repository and running
`make` never produces it, and no build ever fetches it.

Three conditions have to hold before the IRKernel path is built, and they
are **independent of each other on purpose** -- each one can be true while
the others are false:

| # | Condition | Established by | Who does it |
|---|---|---|---|
| 1 | IRKernel is installed | detection | anyone, automatically |
| 2 | a valid licence file is present | validation | the licensee, once |
| 3 | IRKernel is explicitly enabled | `CONF_SCHED_IRKERNEL` plus `USE_IRKERNEL=1` on the command line | the user, per build |

Any combination short of all three builds `sched_aes`.  The difference is
only whether the build says something, and it always says **which** of the
three is missing -- that is the point of keeping them apart:

| 1 installed | 2 licensed | 3 enabled | Result |
|---|---|---|---|
| no | -- | no | `sched_aes`, silently |
| yes | -- | no | `sched_aes`, plus a one-line note: detected, not enabled, separately licensed |
| no | -- | yes | **abort**: IRKernel was requested but is not installed; where to obtain it |
| yes | no | yes | **abort**: IRKernel found, but no valid licence; how to get one |
| yes | yes | yes | `sched_irk`, licence id stamped into the image |

Detection is automatic; enabling never is.  A directory lying next to ours
is not consent, and neither is a licence file: condition 2 says somebody
*may* use it, condition 3 says somebody *chose* to.

Where this lives in the build system we actually have -- make plus
Kconfig (`tools/genconfig.py`, `tools/kconfig.mk`), no CMake:

| Piece | File | Job |
|---|---|---|
| detection (1) | `sched.mk` | `IRK_ROOT` from the command line, the environment, an installed SDK, and `../IRKernel` only as a last resort.  Sets `IRK_FOUND` and `IRK_VERSION` and nothing else |
| validation (2) | `sched.mk` | finds and checks the licence file, sets `IRK_LICENSE_OK`, `IRK_LICENSEE`, `IRK_LICENSE_ID`.  Never reads anything inside a work tree |
| selection (3) | `aes/Kconfig` + `USE_IRKERNEL=1` | a `choice`: `CONF_SCHED_AES` (default) or `CONF_SCHED_IRKERNEL`, whose help text says plainly what it is; the make variable is the act |
| gate | `Makefile` | evaluates the three, picks the backend, and on abort names the condition that failed -- never a generic "cannot build" |

Condition 3, the act itself, is a **make variable, not a config symbol**:

    make ... USE_IRKERNEL=1

Keeping it out of `.config` is deliberate.  Consent stored in a
configuration file is consent nobody remembers giving, and it would travel
with the directory to whoever gets it next.  For everyday work `local.mk`
may set it -- that file is untracked and has to be created on purpose.


### The licence file

It belongs in **no repository**: not in GEMbedded, not in IRKernel.  It is
per licensee, not per project, and a repository is exactly the thing that
gets cloned and passed on.

Search order, none of it inside a work tree:

1. `IRKERNEL_LICENSE=<path>` on the make command line
2. the `IRKERNEL_LICENSE` environment variable
3. `$XDG_CONFIG_HOME/irkernel/license`, else `~/.irkernel/license`

`detect.mk` **refuses a path that resolves inside either work tree** and
says why.  That is not pedantry: a licence file inside the tree is one
`git add -A` away from being published, and it carries a name and a
licence id.  Both repositories also carry `.gitignore` entries for
`*.license` as a second net.

"Valid" means well-formed and applicable: licensee, product, permitted
use, licence id, the ABI version it was issued for.  A signature (private
key with the vendor, public key in the checker) makes the file checkable
rather than merely readable.

Where the check sits matters less than how the kernel is delivered.  In
the GPL adapter it is readable and changeable by anyone, so it documents
rather than enforces.  Inside IRKernel it is harder to remove -- but only
as hard as the delivery form makes it: shipped as a binary that means
something, shipped as source it is theatre.  What actually prevents
unlicensed use is that IRKernel is not downloadable anywhere.  The
manifest records who agreed to what, which is what a licence gate is for.

One rule holds regardless of where the check lives: **a failed check never
stops the kernel from scheduling.**  `irk_license_valid()` in
`IRKernel.h` reports; the kernel never asks.  A scheduler that withholds
its work over a contractual question stops a machine, and it will do so at
the worst moment and over the pettiest cause -- a clock, a file that did
not get copied, a flash error.  If enforcement is ever wanted, it belongs
in a clearly labelled evaluation edition, never in the one that runs a
robot.

`__has_include` appears in the adapter only as a safety net: `#error` when
the configuration is active but the kernel is missing.  Never to switch
anything on.  Presence is not consent.


## What the seam does not decide

Whether a distributed combination counts as two programs or as one
derived work.  No switch and no checkbox decides that, and it is worth
being blunt about it here so that nobody later mistakes the gate for a
licence.

The gate carries the **contract**: who chose, what they accepted, which
licence id sits in which image.  The **architecture** carries the
copyright question, and only while these five properties hold:

1. pTOS is complete and fully functional without IRKernel.  `sched_aes` is
   not a stub kept alive for appearances -- it is the default, and it
   stays maintained.
2. IRKernel is a product in its own right (CNC, robotics, plain embedded),
   with an ABI useful to someone who has never heard of GEM.
3. The build never downloads IRKernel and never ships it as a locked blob
   "just in case".
4. The user obtains it separately and enables it explicitly.
5. Only generic kernel operations cross the seam.  No `AESPD *`, no `PD *`,
   no `rlr`, no `nrl`, no `EVB *`.  And the calls carry neutral names
   (`k_*`): an interface named after one implementation is not an
   interface.

The strongest form is the one rtcore already uses on core 1 -- a separate
image at its own flash address, loaded at runtime, reached only through a
versioned ABI.  Applied to the scheduler it costs an `SVC` per switch,
which a cooperative system takes rarely.  The open question there is who
owns the `SVC` vector, since pTOS already uses it for the TOS traps
(`aes/arch/armv8m/gemasm.S`, `aestrap` and the `svc #255` resume
trampoline).

Notes, not legal advice.  [licensing.md](licensing.md) has the three ways
out and what each one costs.


## Open questions

- Does anything in the AES rely on strict round robin?  The screen
  manager and `gl_mowner` are the places to check first.
- IRKernel's deadlock detection (`IRK_DEADLOCK_CYCLES`) versus an AES that
  is legitimately idle with every process waiting.
- `NUM_PDS`, the hardcoded `pd_index()` and `gl_mntree` as a single global
  menu tree all block real multi-app, independently of this seam.  What
  else stands in the way of a program being a task is in
  [multitasking.md](multitasking.md) -- chiefly that GEMDOS' `run` does
  not follow a task switch today.


## Steps

1. **A** -- introduce the seam, implement `sched_aes.c`, change nothing
   observable.  Verifiable: the desktop behaves exactly as before.
2. **B** -- the gate: `sched.mk`, `aes/Kconfig`, the three states
   and the licence manifest.  Verifiable without IRKernel being present at
   all -- the "not installed" and "available" paths are the two that every
   user will see.
3. **C** -- `sched_irk.c` in mode 2 (linked), measured against A.
4. **D** -- mode 3, the separate image, when a product is actually
   redistributed to third parties.  Not before: the work is real, and
   mode 2 answers every question except that one.
5. **E** -- multi-app and cyclic tasks exposed to GEM programs in the SDK.
