/*
 * Fractals - three computations sharing the real-time core, and a window
 *            to change how they share it
 *
 * The program is in fractals.c, next to this file, in plain C: that is
 * what GEM programs are written in, and the Arduino IDE compiles every
 * .c file of a sketch as C.  This file, the .ino, would be compiled as
 * C++; it is left as a note so that nothing gets in the way of the
 * program's own main().
 *
 * Four windows.  Three of them show a picture that a task on the other
 * core is computing -- a Mandelbrot set, a Julia set whose parameter
 * drifts, and a Lissajous figure that waves.  The fourth has a slider for
 * each task, and next to it the share of the processor that task actually
 * received, measured.
 *
 * That pairing is the whole point.  IRKernel's priority is a share, not a
 * rank: at 200 a task gets twice the time of one at 100, and neither
 * starves.  So you set a ratio with the sliders and read back, beside it,
 * that the ratio was kept.
 *
 * The Lissajous figure is the counterexample in the same picture.  It is
 * not share-scheduled but cyclic, every 25 ms, because it is periodic by
 * nature: it should wave evenly, not quickly.  Move the sliders and the
 * two fractals visibly speed up and slow down while the figure keeps its
 * tempo.  Its worst lateness is in the panel too.
 *
 * Needs the PSRAM: the three canvases are 450 KB and go to Alt-RAM.
 * And nothing may write the internal flash while it runs -- that takes
 * both chip selects for 23 ms, and the tasks would be reading a bus that
 * is not answering.  No saving, no deploying during the run.
 *
 * Press Upload with DEPLOY running on the machine.
 */
