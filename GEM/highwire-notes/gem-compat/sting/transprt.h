/*
 * sting/transprt.h - the name STinG's clients include, pointing at ours
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * A program written for STiK or STinG includes <sting/transprt.h> and
 * gets the transport table, the error codes and the one-word macros that
 * call through the table it holds in 'tpl'. This is that header for this
 * machine: the declarations come from pTOS's own include/stik.h, so
 * there is one description of the interface rather than two that have to
 * be kept in step.
 *
 * STinG's original also brings in its IP, ICMP and port layers, which a
 * client of the transport does not use. They are left out rather than
 * reproduced; anything that genuinely needs them wants the real STinG
 * headers and a real STinG underneath.
 */

#ifndef GEMBEDDED_STING_TRANSPRT_H
#define GEMBEDDED_STING_TRANSPRT_H

#include <stik.h>

/*
 * The widths STinG's own headers name. A client declares its address
 * lists and ports with these, so they belong to the interface as much as
 * the function table does.
 */
typedef signed char     int8;
typedef unsigned char   uint8;
typedef short           int16;
typedef unsigned short  uint16;
typedef long            int32;
typedef unsigned long   uint32;

/* STinG spells the cookie magic this way */
#define STIK_COOKIE_MAGIC   STIK_COOKIE

/*
 * The calls, as a client writes them. Each one goes through the table
 * the client found and keeps in 'tpl' -- the name is part of the
 * interface, and every STiK program declares it.
 */
#define get_dftab(x)            (*drivers->get_dftab)(x)
#define ETM_exec(x)             (*drivers->ETM_exec)(x)

#define KRmalloc(a)             (*tpl->KRmalloc)(a)
#define KRfree(a)               (*tpl->KRfree)(a)
#define get_err_text(a)         (*tpl->get_err_text)(a)
#define carrier_detect()        (*tpl->carrier_detect)()

#define TCP_open(a,b,c,d)       (*tpl->TCP_open)(a,b,c,d)
#define TCP_close(a,b,c)        (*tpl->TCP_close)(a,b,c)
#define TCP_send(a,b,c)         (*tpl->TCP_send)(a,b,c)

#define CNkick(a)               (*tpl->CNkick)(a)
#define CNbyte_count(a)         (*tpl->CNbyte_count)(a)
#define CNget_char(a)           (*tpl->CNget_char)(a)
#define CNget_block(a,b,c)      (*tpl->CNget_block)(a,b,c)
#define CNgets(a,b,c,d)         (*tpl->CNgets)(a,b,c,d)
#define CNgetinfo(a)            (*tpl->CNgetinfo)(a)

#define resolve(a,b,c,d)        (*tpl->resolve)(a,b,c,d)

#endif /* GEMBEDDED_STING_TRANSPRT_H */
