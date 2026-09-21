/*
 * kapi.h - inside the kernel: the call table's helpers
 */

#ifndef KAPI_H
#define KAPI_H

long kapi_start_core1(void);    /* boot.c */
void kapi_idle(void);           /* core 0's idle, from irk_port_idle() */

#endif /* KAPI_H */
