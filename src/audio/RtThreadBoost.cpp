#include "audio/RtThreadBoost.h"

#include <cstdint>

#if defined(__APPLE__)

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>

namespace {
// Convert nanoseconds to mach absolute-time ticks using the system timebase.
std::uint64_t nsToTicks(double ns) {
  mach_timebase_info_data_t tb;
  mach_timebase_info(&tb);
  const double ticks = ns * static_cast<double>(tb.denom) /
                       static_cast<double>(tb.numer);
  return static_cast<std::uint64_t>(ticks);
}
}  // namespace

bool BoostCurrentThreadForRt() noexcept {
  // QoS first: marks the thread as user-interactive for the scheduler and
  // for any mach work done through XPC/servers.
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

  // Time-constraint policy: the scheduler must give us `computation` ticks of
  // CPU in every `period`-tick window, else we run at the real-time maximum.
  // A 2 ms period covers a 64-sample block (1.33 ms @ 48 kHz) with headroom.
  thread_time_constraint_policy_data_t tc;
  const double periodNs = 2.0 * 1000.0 * 1000.0;  // 2 ms
  tc.period = static_cast<uint32_t>(nsToTicks(periodNs));
  tc.computation = static_cast<uint32_t>(nsToTicks(periodNs * 0.75));
  tc.constraint = static_cast<uint32_t>(nsToTicks(periodNs * 0.9));
  tc.preemptible = 1;

  const kern_return_t kr = thread_policy_set(
      mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY,
      reinterpret_cast<thread_policy_t>(&tc), THREAD_TIME_CONSTRAINT_POLICY_COUNT);
  return kr == KERN_SUCCESS;
}

#elif defined(__linux__)

#include <pthread.h>
#include <sched.h>

bool BoostCurrentThreadForRt() noexcept {
  sched_param sp{};
  sp.sched_priority = 80;
  return pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0;
}

#else

bool BoostCurrentThreadForRt() noexcept { return false; }

#endif