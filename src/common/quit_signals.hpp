#pragma once

// Every Fleetwm program turns SIGTERM and SIGINT into a signalfd that the event loop reads, so a plain `kill`
// ends it through its normal exit path (profile data flushed, PipeWire and Wayland connections closed). That only
// works while the signals are blocked in EVERY thread: a thread that starts before the block (PipeWire's loop thread,
// started by the volume readout or the mixer) keeps SIGTERM unblocked, and the kernel is free to deliver the
// signal to it, which kills the whole process on the spot. block_quit_signals() therefore has to be the first
// thing main() does, before any library has had the chance to start a thread; new threads inherit the mask.

#include <pthread.h>
#include <signal.h>

namespace fleetwm {

inline void block_quit_signals() {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  pthread_sigmask(SIG_BLOCK, &mask, nullptr);
}

}  // namespace fleetwm
