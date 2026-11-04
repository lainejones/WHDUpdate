# StackSwap probes

Small programs that found the 0.6.x exit crash on the A500 (2026-11-03):
`hello3.c` (StackSwap, the swapped function takes no arguments) runs;
`hello9.c` (the same, but the swapped function takes arguments) takes the
machine down at exit - gcc pops the pushed arguments after the swap back,
on the old stack. Build like the Makefile does and run each under `Run`
with its output redirected to a file; the marks land in `TD0:` (a host
directory on the WinUAE A500) so they survive the crash. The others are
the steps in between.
