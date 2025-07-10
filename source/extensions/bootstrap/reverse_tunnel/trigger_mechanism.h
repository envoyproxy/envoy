#pragma once

#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"

#include "source/common/common/logger.h"

#ifdef __APPLE__
#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(__linux__)
#include <sys/eventfd.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Cross-platform trigger mechanism interface.
 * Provides optimal implementation for each platform:
 * - macOS: kqueue EVFILT_USER (no file descriptor overhead)
 * - Linux: eventfd (single FD, 64-bit counter)
 * - Other Unix: pipe (fallback for compatibility)
 */
class TriggerMechanism : public Logger::Loggable<Logger::Id::connection> {
public:
  virtual ~TriggerMechanism() = default;

  /**
   * Initialize the trigger mechanism.
   * @param dispatcher the event dispatcher to use
   * @return true if successful, false otherwise
   */
  virtual bool initialize(Event::Dispatcher& dispatcher) = 0;

  /**
   * Trigger the mechanism to wake up waiting threads.
   * @return true if successful, false otherwise
   */
  virtual bool trigger() = 0;

  /**
   * Wait for a trigger event (non-blocking check).
   * @return true if triggered, false if no trigger pending
   */
  virtual bool wait() = 0;

  /**
   * Get the file descriptor for event loop monitoring.
   * @return file descriptor, or -1 if not applicable
   */
  virtual int getMonitorFd() const = 0;

  /**
   * Get a description of the trigger mechanism type.
   * @return string description
   */
  virtual std::string getType() const = 0;

  /**
   * Reset the trigger mechanism to initial state.
   */
  virtual void reset() = 0;

  /**
   * Factory method to create the best trigger mechanism for the current platform.
   * @return unique pointer to the trigger mechanism
   */
  static std::unique_ptr<TriggerMechanism> create();
};

#ifdef __APPLE__
/**
 * macOS-specific implementation using kqueue EVFILT_USER.
 * No file descriptor overhead, best performance on macOS.
 */
class KqueueUserTrigger : public TriggerMechanism {
public:
  KqueueUserTrigger() : kqueue_fd_(-1), user_event_ident_(0) {}
  ~KqueueUserTrigger() override;

  bool initialize(Event::Dispatcher& dispatcher) override;
  bool trigger() override;
  bool wait() override;
  int getMonitorFd() const override { return kqueue_fd_; }
  std::string getType() const override { return "kqueue_user"; }
  void reset() override;

private:
  int kqueue_fd_;
  uintptr_t user_event_ident_;
};
#endif

#ifdef __linux__
/**
 * Linux-specific implementation using eventfd.
 * Single file descriptor, 64-bit counter, very efficient.
 */
class EventfdTrigger : public TriggerMechanism {
public:
  EventfdTrigger() : eventfd_(-1) {}
  ~EventfdTrigger() override;

  bool initialize(Event::Dispatcher& dispatcher) override;
  bool trigger() override;
  bool wait() override;
  int getMonitorFd() const override { return eventfd_; }
  std::string getType() const override { return "eventfd"; }
  void reset() override;

private:
  int eventfd_;
};
#endif

/**
 * Fallback implementation using pipes for maximum compatibility.
 * Works on all Unix systems but uses two file descriptors.
 */
class PipeTrigger : public TriggerMechanism {
public:
  PipeTrigger() : read_fd_(-1), write_fd_(-1) {}
  ~PipeTrigger() override;

  bool initialize(Event::Dispatcher& dispatcher) override;
  bool trigger() override;
  bool wait() override;
  int getMonitorFd() const override { return read_fd_; }
  std::string getType() const override { return "pipe"; }
  void reset() override;

private:
  int read_fd_;
  int write_fd_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
