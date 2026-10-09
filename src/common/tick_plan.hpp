// Several periodic jobs that share one timer. Every job is due on a multiple of its period (counted in wall-clock
// milliseconds), so jobs with periods of 1 s, 2 s, 5 s and 15 s all fall on the same second and the process wakes once
// for all of them instead of once per job. Pure arithmetic: the caller owns the timer.
#pragma once

#include <algorithm>
#include <vector>

namespace fleetwm {

class TickPlan {
 public:
  // Adds a job; period 0 means off. Returns its id. The first run is the next multiple of the period after `now_ms`.
  int add(long period_ms, long now_ms) {
    jobs_.push_back({period_ms, 0});
    set_period(static_cast<int>(jobs_.size()) - 1, period_ms, now_ms);
    return static_cast<int>(jobs_.size()) - 1;
  }

  void set_period(int id, long period_ms, long now_ms) {
    Job& j = jobs_[static_cast<size_t>(id)];
    j.period = period_ms;
    j.due = period_ms > 0 ? next_boundary(now_ms, period_ms) : 0;
  }

  // The ids of the jobs due at `now_ms`, each moved on to its next boundary after `now_ms`.
  std::vector<int> take_due(long now_ms) {
    std::vector<int> due;
    for (size_t i = 0; i < jobs_.size(); ++i) {
      Job& j = jobs_[i];
      if (j.period <= 0 || now_ms < j.due) continue;
      due.push_back(static_cast<int>(i));
      j.due = next_boundary(now_ms, j.period);
    }
    return due;
  }

  // Milliseconds until the next job is due (at least 1), or -1 when every job is off.
  long wait_ms(long now_ms) const {
    long best = -1;
    for (const Job& j : jobs_) {
      if (j.period <= 0) continue;
      const long w = std::max(1L, j.due - now_ms);
      if (best < 0 || w < best) best = w;
    }
    return best;
  }

 private:
  struct Job {
    long period;
    long due;
  };
  static long next_boundary(long now_ms, long period) { return (now_ms / period + 1) * period; }
  std::vector<Job> jobs_;
};

}  // namespace fleetwm
