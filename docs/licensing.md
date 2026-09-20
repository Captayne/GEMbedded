# Licensing

Notes, not legal advice.  The decision on IRKernel is still open; this is
what it hinges on.

## The three parts

| Part | Author | Licence |
|---|---|---|
| pTOS (EmuTOS descendant, `pTOS/`) | pTOS and EmuTOS developers, plus our changes | **GPL v2 or later** |
| IRKernel | Andreas Keibel | own: free for non-commercial use, 0.30 EUR per device commercially |
| everything in this repository | Andreas Keibel | **MIT** (`LICENSE`) |

The SDK headers that programs include (`pTOS/include/rtx.h`, `touch.h`,
`usbcon.h`) are MIT even though they live in the GPL repository, so that a
program using them is not bound by the GPL.

## Where the two meet

Today IRKernel is only in **rtcore**, the runtime on core 1.  That is a
separate image at its own flash address, started by pTOS but not linked
with it.  Two programs shipped side by side are an aggregation, not one
work, so pTOS' GPL does not reach into IRKernel.

That changes if IRKernel becomes **the scheduler of the AES** (the plan
discussed on 2026-09-20): it would be compiled into the pTOS binary.
Distributing that binary means distributing one combined work, which the
GPL requires to be distributable under the GPL.  IRKernel would then need
a GPL-compatible licence for that use.

## The ways out

**A -- dual licence IRKernel (GPL v2+ OR commercial).**  As its sole
author, Andreas Keibel can offer it under both.  GEMbedded uses the GPL
arm; the Arduino world keeps buying commercial licences for IRKernel on
its own.  Worth knowing: inside a GPL system a per-device fee cannot
really be enforced, because whoever receives the combined work may pass it
on.  The commercial value stays with IRKernel *outside* GEMbedded.

There is a fourth shape, and it is the one being designed:
**B'** -- keep IRKernel a separate program even when the AES uses it.  Its
own image at its own flash address, loaded at runtime, reached only
through a versioned ABI, with pTOS complete and fully functional without
it.  Two programs shipped together are an aggregation, so the per-device
fee stays enforceable and pTOS stays GPL.  That is the rtcore model,
applied to core 0.

[scheduler-abi.md](scheduler-abi.md) has that side: the seam, the build
gate that makes enabling IRKernel a deliberate act of the user, and the
five properties the separation depends on.  The gate carries the
contract -- it does not decide the question above.

**B -- keep IRKernel out of the kernel.**  It stays on core 1 and as a
library for applications, and the AES keeps its own dispatcher.  Full
control over the licence, but the scheduler idea is off the table -- that
is the price.

**C -- MIT or BSD for IRKernel.**  Widest adoption, no licence income.

## What this repository does meanwhile

Our own code is MIT, our changes to pTOS are GPL v2+ like the rest of
pTOS, and rtcore links IRKernel under its current licence.  Nothing here
forecloses A, B or C.
