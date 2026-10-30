#pragma once

namespace onion::daemon::hyper {
enum class ReadyWaitResult { Ready, Cancelled, Installing, Exited, TimedOut };

// The installation marker takes priority over READY. Keep this wait separate
// from the worker so an installer can acquire its exclusive asset lock even
// when STT initialization has not completed yet.
template <typename Allowed, typename Installing, typename Ready,
          typename Alive, typename Pause>
ReadyWaitResult wait_for_ready(Allowed allowed, Installing installing,
                               Ready ready, Alive alive, Pause pause,
                               unsigned polls) {
  for (unsigned i = 0; i < polls; ++i) {
    if (!allowed()) return ReadyWaitResult::Cancelled;
    if (installing()) return ReadyWaitResult::Installing;
    if (ready()) {
      if (!allowed()) return ReadyWaitResult::Cancelled;
      return installing() ? ReadyWaitResult::Installing : ReadyWaitResult::Ready;
    }
    if (!alive()) return ReadyWaitResult::Exited;
    pause();
  }
  return ReadyWaitResult::TimedOut;
}
} // namespace onion::daemon::hyper
