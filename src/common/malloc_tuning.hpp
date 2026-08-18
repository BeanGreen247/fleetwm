#pragma once

namespace fleetwm {

// Pins glibc's mmap/trim thresholds to a fixed 64KB instead of leaving
// them on glibc's default fully-dynamic behavior, which only ever grows
// them (never shrinks back down) every time a large-enough chunk gets
// freed. Long-running fleetwm processes that spawn/destroy lots of
// short-lived objects over time -- the compositor opening/closing
// windows, the bar's systray icons coming and going, any GTK4 client's
// theme/config reloads -- otherwise accumulate freed-but-never-returned
// memory in the heap arena instead of giving it back to the OS.
//
// Confirmed live on fleetwm-dev: the compositor's Pss climbed from
// ~90MB to 150MB+ and stayed there after spawning and closing ~90
// terminals, and a one-off `malloc_trim(0)` reclaimed the same ~60MB
// this permanently avoids needing to do. Call once, as early as
// possible in main() -- before any real allocation activity, so the
// fixed thresholds are in effect from the start rather than after
// glibc's dynamic adjustment has already ratcheted them up.
void tune_malloc_for_low_rss();

}  // namespace fleetwm
