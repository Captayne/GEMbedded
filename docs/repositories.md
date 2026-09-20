# Repositories

Three repositories, each keeping its own history and licence:

    pTOS3000            this one: rtcore, apps, tools, docs      MIT
      pTOS/             fork of kelihlodversson/pTOS             GPL v2+
      (IRKernel)        linked into rtcore from its own repo     own licence

`pTOS/` and IRKernel are meant to be git submodules of this repository
once they are on a server; until then they are ordinary directories
(`pTOS/` is excluded by `.gitignore`, IRKernel is built from
`D:\Develop\_IRF\IRKernel`).

## Why separate

* The pTOS fork keeps upstream's history, so that fixes can go back as
  clean pull requests.  Four of them are waiting: the 50 Hz timer that was
  never called, the signed rotation of `timer_c_sieve`, the button bits in
  the ARM `mouse_int()`, and the `gl_bpend` leak in the AES.  They affect
  every ARM target of pTOS, not just ours.
* IRKernel is used outside this project as well.
* The licences stay apart and visible (see `licensing.md`).

## Putting it on a server (not done yet)

Nothing has been published.  When it is time:

    # 1. fork kelihlodversson/pTOS on GitHub, then point the clone at it
    cd pTOS
    git remote rename origin upstream
    git remote add origin git@github.com:<user>/pTOS.git
    git push -u origin feature/rp2350-port

    # 2. this repository
    cd ..
    git remote add origin git@github.com:<user>/pTOS3000.git
    git push -u origin master

    # 3. tie them together
    git submodule add git@github.com:<user>/pTOS.git pTOS
    git submodule add git@github.com:<user>/IRKernel.git third_party/IRKernel

Then `git clone --recursive` brings everything.  Private repositories
work the same way and can be made public later.

## The pull requests to upstream

pTOS asks for an issue first, then a branch, then a draft pull request.
The four fixes above are independent of the RP2350 port and are the
natural first contribution; the port itself is much larger and would be a
separate conversation with upstream.
