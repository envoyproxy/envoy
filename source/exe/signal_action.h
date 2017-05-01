#pragma once

#include <signal.h>
#include <unistd.h>

#include "common/common/non_copyable.h"

#include "server/backtrace.h"

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
 * used for the alternate signal stack is destroyed and the default signal handler
 * is restored.
 *
 * NOTE: Existing non-default signal handlers are overridden and will not be
 * restored. If this behavior is ever required it can be implemented.
 *
 * It is recommended that this object be instantiated at the highest possible
 * scope, eg, in main(). This enables fatal signal handling for almost all code
 * executed.
 */
class SignalAction : NonCopyable {
public:
  SignalAction()
      : guard_size_(sysconf(_SC_PAGE_SIZE)), altstack_size_(guard_size_ * 4), altstack_(nullptr) {
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

private:
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
   * Initialized as a multiple of page size (although sigaltstack will
   * do alignment as needed).
   *
   * Additionally, two guard pages will be allocated to bookend the usable area.
   */
  const size_t altstack_size_;
  /**
   * This constant array defines the signals we will insert handlers for.
   *
   * Essentially this is the list of signals that would cause a core dump.
   */
  static constexpr int FATAL_SIGS[] = {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV};
  /**
   * Return the memory size we actually map including two guard pages.
   */
  size_t mapSizeWithGuards() const { return altstack_size_ + guard_size_ * 2; }
  /**
   * The actual signal handler function with prototype matching signal.h
   */
  static void sigHandler(int sig, siginfo_t* info, void* context);
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
  char* altstack_;
};
