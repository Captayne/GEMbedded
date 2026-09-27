/*
 * cxx.cpp - the little that C++ needs and a TOS program has not got
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * Classes, virtual functions and global objects work without any of
 * this: libcmini's start-up already runs the global constructors (its
 * __init_array walk).  What the compiler then asks for is a handful of
 * names, and here they are:
 *
 *   new / delete          the heap of the C library
 *   __aeabi_atexit        "run this destructor when the program ends".
 *                         It does not: a GEM program ends with Pterm,
 *                         which hands its memory back in one piece, and
 *                         a destructor that only frees memory has
 *                         nothing left to do.  Keeping the list would
 *                         cost memory for no gain.
 *   __dso_handle          which module a destructor belongs to.  There
 *                         is one module here, so any address will do.
 *   __cxa_pure_virtual    calling a method that does not exist: stop.
 *
 * Exceptions and RTTI stay off (platform.txt), so nothing of the
 * standard library is needed.  That is what makes C++ affordable here.
 */

#include <stdlib.h>

void *operator new(size_t n)            { return malloc(n); }
void *operator new[](size_t n)          { return malloc(n); }
void  operator delete(void *p)          { free(p); }
void  operator delete[](void *p)        { free(p); }
void  operator delete(void *p, size_t)  { free(p); }
void  operator delete[](void *p, size_t){ free(p); }

extern "C" {

void *__dso_handle = 0;

int __aeabi_atexit(void *object, void (*destructor)(void *), void *dso)
{
    (void)object;
    (void)destructor;
    (void)dso;
    return 0;
}

void __cxa_pure_virtual(void)
{
    abort();
}

}

/*
 * And the global constructors themselves.
 *
 * libcmini's start-up does not run them -- that is part of what makes it
 * mini -- so the linker is told to send the start-up's call to main()
 * here instead (-Wl,--wrap=main in platform.txt).  We walk the table the
 * compiler left in .init_array, then let the program begin.
 *
 * The destructors at the end are left out on purpose: see above.
 */
extern "C" {

extern void (*__preinit_array_start[])(void);
extern void (*__preinit_array_end[])(void);
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

int __real_main(void);

int __wrap_main(void)
{
    long i, n;

    n = __preinit_array_end - __preinit_array_start;
    for (i = 0; i < n; i++)
        __preinit_array_start[i]();

    n = __init_array_end - __init_array_start;
    for (i = 0; i < n; i++)
        __init_array_start[i]();

    return __real_main();
}

}
