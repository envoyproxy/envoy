#pragma once

#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {
/**
 * Tries to terminates the process by killing the thread specified by
 * the ThreadId. The implementation is platform dependent and currently
 * only works on platforms that support signals.
 *
 * Returns true if the platform specific function to terminate the thread
 * succeeded (i.e. kill() == 0). If the platform is currently unsupported, this
 * will return false.
 */
bool terminateThread(const ThreadId& tid);

/**
 * Tries to send the provided signal to the specific thread identified by the
 * ThreadId (and only that thread).
 *
 * This is intentionally distinct from terminateThread(): it requires the signal
 * to be delivered to one particular thread so that, e.g., a per-thread signal
 * handler runs on the targeted thread.
 *
 * Returns true on success, false on unsupported platform or syscall failure.
 */
bool signalThread(const ThreadId& tid, int signal);
} // namespace Thread
} // namespace Envoy
