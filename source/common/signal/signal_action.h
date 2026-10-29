#pragma once

#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <list>

#include "source/common/common/non_copyable.h"
#include "source/common/signal/fatal_error_handler.h"
#include "source/server/backtrace.h"

namespace Envoy {

/**
 * This class installs signal handlers for fatal signal types.
 *
 * These signals are handled:
 *   SIGABRT
 *   SIGBUS
 *   SIGFPE
 *   SIGILL
 *   SIGSEGV
 *
 * Upon intercepting the signal the following actions are taken:
 *
 *   A Backtrace is printed from the address the signal was encountered at, if
 *   it is possible to retrieve.
 *
 *   The signal handler is reset to the default handler (which is expected to
 *   terminate the process).
 *
 *   The signal is raised again (which ultimately kills the process)
 *
 * The signal handler must run on an alternative stack so that we can do the
 * stack unwind on the original stack. Memory is allocated for this purpose when
 * this object is constructed. When this object goes out of scope the memory
 * used for the alternate signal stack is destroyed and the previous signal handler
 * and alt stack if previously used are restored.
 *
 * Note that we do NOT restore the previously saved sigaction and alt stack in
 * the signal handler itself. This is fraught with complexity and has little
 * benefit. The inner most scope SignalAction will terminate the process by
 * re-raising the fatal signal with default handler.
 *
 * It is recommended that this object be instantiated at the highest possible
 * scope, e.g., in main(). This enables fatal signal handling for almost all code
 * executed. Because of the save-and-restore behavior it is possible for
 * SignalAction to be used at both wider and tighter scopes without issue.
 */
class SignalAction : NonCopyable {
public:
  SignalAction()
      : guard_size_(sysconf(_SC_PAGE_SIZE)), altstack_size_(computeAltStackSize(guard_size_)) {
    mapAndProtectStackMemory();
    installSigHandlers();
  }
  ~SignalAction() {
    removeSigHandlers();
    unmapStackMemory();
  }
  /**
   * Helpers for testing guarded stack memory
   */
  void doGoodAccessForTest();
  void tryEvilAccessForTest(bool end);
  /**
   * The actual signal handler function with prototype matching signal.h
   *
   * Public so that we can exercise it directly from a test.
   */
  static void sigHandler(int sig, siginfo_t* info, void* context);

  /**
   * Async-signal-safe description of a fatal signal. Unlike `strsignal()`,
   * returns static storage without allocating, so it is safe to call from
   * the signal handler even when the fault originates inside the allocator.
   *
   * Public so that it can be exercised directly from a test.
   */
  static const char* signalDescription(int sig);

private:
  static size_t computeAltStackSize(size_t page) {
    size_t min_size = MINSIGSTKSZ;
#ifdef _SC_SIGSTKSZ
    const long dyn = sysconf(_SC_SIGSTKSZ);
    if (dyn > 0) {
      min_size = std::max(min_size, static_cast<size_t>(dyn));
    }
#endif
    const size_t want = std::max(page * 16, min_size * 4);
    return (want + page - 1) / page * page;
  }
  /**
   * Allocate this many bytes on each side of the area used for alt stack.
   *
   * Set to system page size.
   *
   * The memory will be protected from read and write.
   */
  const size_t guard_size_;
  /**
   * Use this many bytes for the alternate signal handling stack.
   *
   * Sized with headroom for nested signal delivery and the handler's backtrace
   * and formatting work. The runtime `_SC_SIGSTKSZ` value accounts for larger
   * signal frames on platforms such as aarch64 with `SVE`; `MINSIGSTKSZ` is used
   * as a fallback where the runtime value is unavailable. The size is a
   * multiple of page size (although signalstack will do alignment as needed).
   *
   * Additionally, two guard pages will be allocated to bookend the usable area.
   */
  const size_t altstack_size_;
  /**
   * This constant array defines the signals we will insert handlers for.
   *
   * Essentially this is the list of signals that would cause a core dump.
   * The object will contain an array of struct sigactions with the same number
   * of elements that are in this array.
   */
  static constexpr int FATAL_SIGS[] = {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV};
  /**
   * Return the memory size we actually map including two guard pages.
   */
  size_t mapSizeWithGuards() const { return altstack_size_ + guard_size_ * 2; }
  /**
   * Install all signal handlers and setup signal handling stack.
   */
  void installSigHandlers();
  /**
   * Remove all signal handlers.
   *
   * Must be executed before the alt stack memory goes away.
   *
   * Signal handlers will be reset to the default, NOT back to any signal
   * handler existing before InstallSigHandlers().
   */
  void removeSigHandlers();
  /**
   * Use mmap to map anonymous memory for the alternative stack.
   *
   * GUARD_SIZE on either end of the memory will be marked PROT_NONE, protected
   * from all access.
   */
  void mapAndProtectStackMemory();
  /**
   * Unmap alternative stack memory.
   */
  void unmapStackMemory();
  char* altstack_{};
  std::array<struct sigaction, sizeof(FATAL_SIGS) / sizeof(int)> previous_handlers_;
// sigaltstack and backtrace() are incompatible on Apple platforms
// https://reviews.llvm.org/D28265
#if !defined(__APPLE__)
  stack_t previous_altstack_;
#endif
};

} // namespace Envoy
