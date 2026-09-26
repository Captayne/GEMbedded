/*
 * Core0Latency - how long can core 0 be away?
 *
 * The program is in core0lat.c, in plain C.  It measures from the other
 * core, because that one cannot be delayed: a cyclic task there watches a
 * timestamp the GEM half refreshes, and keeps the worst gap.
 *
 * Three rounds: waiting, drawing, writing to F:.
 */
