#include "source/extensions/bootstrap/reverse_tunnel/trigger_mechanism.h"

#include <fcntl.h>

#include <cerrno>
#include <cstring>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Factory method to create the best trigger mechanism for the current platform
std::unique_ptr<TriggerMechanism> TriggerMechanism::create() {
#ifdef __APPLE__
  ENVOY_LOG(debug, "Creating kqueue user event trigger for macOS");
  return std::make_unique<KqueueUserTrigger>();
#elif defined(__linux__)
  ENVOY_LOG(debug, "Creating eventfd trigger for Linux");
  return std::make_unique<EventfdTrigger>();
#else
  ENVOY_LOG(debug, "Creating pipe trigger for generic Unix");
  return std::make_unique<PipeTrigger>();
#endif
}

#ifdef __APPLE__
// macOS kqueue EVFILT_USER implementation
KqueueUserTrigger::~KqueueUserTrigger() {
  ENVOY_LOG(debug, "KqueueUserTrigger destructor - cleaning up kqueue FD {}", kqueue_fd_);
  if (kqueue_fd_ != -1) {
    ENVOY_LOG(debug, "Closing kqueue FD: {}", kqueue_fd_);
    if (::close(kqueue_fd_) == -1) {
      ENVOY_LOG(error, "Failed to close kqueue FD {}: {}", kqueue_fd_, strerror(errno));
    } else {
      ENVOY_LOG(debug, "Successfully closed kqueue FD: {}", kqueue_fd_);
    }
    kqueue_fd_ = -1;
  }
  ENVOY_LOG(debug, "KqueueUserTrigger destructor complete");
}

bool KqueueUserTrigger::initialize(Event::Dispatcher& dispatcher) {
  (void)dispatcher; // Unused - Envoy listener monitors FD directly
  // Create kqueue file descriptor
  kqueue_fd_ = ::kqueue();
  if (kqueue_fd_ == -1) {
    ENVOY_LOG(error, "Failed to create kqueue: {}", strerror(errno));
    return false;
  }

  // Generate unique identifier for user event
  user_event_ident_ = reinterpret_cast<uintptr_t>(this);

  // Add user event to kqueue
  struct kevent event;
  EV_SET(&event, user_event_ident_, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);

  if (::kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr) == -1) {
    ENVOY_LOG(error, "Failed to add user event to kqueue: {}", strerror(errno));
    ::close(kqueue_fd_);
    kqueue_fd_ = -1;
    return false;
  }

  // NOTE: We don't register file events here because Envoy's listener
  // will monitor our kqueue FD directly via fdDoNotUse() override

  ENVOY_LOG(debug, "Initialized kqueue user trigger with FD: {}, ident: {}", kqueue_fd_,
            user_event_ident_);
  return true;
}

bool KqueueUserTrigger::trigger() {
  if (kqueue_fd_ == -1) {
    ENVOY_LOG(error, "kqueue not initialized");
    return false;
  }

  ENVOY_LOG(debug, "Triggering kqueue user event on FD {} with ident {}", kqueue_fd_,
            user_event_ident_);

  // Trigger the user event
  struct kevent event;
  EV_SET(&event, user_event_ident_, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);

  if (::kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr) == -1) {
    ENVOY_LOG(error, "Failed to trigger user event: {}", strerror(errno));
    return false;
  }

  ENVOY_LOG(info, "Successfully triggered kqueue user event on FD {} - should be readable now",
            kqueue_fd_);
  return true;
}

bool KqueueUserTrigger::wait() {
  if (kqueue_fd_ == -1) {
    ENVOY_LOG(debug, "kqueue wait called but FD not initialized");
    return false;
  }

  ENVOY_LOG(debug, "Checking kqueue FD {} for user events", kqueue_fd_);

  // Non-blocking check for events
  struct kevent event;
  struct timespec timeout = {0, 0}; // Non-blocking

  int result = ::kevent(kqueue_fd_, nullptr, 0, &event, 1, &timeout);
  if (result == -1) {
    if (errno != EINTR) {
      ENVOY_LOG(error, "kevent wait failed: {}", strerror(errno));
    }
    return false;
  }

  ENVOY_LOG(debug, "kevent returned {} events", result);

  if (result == 1) {
    ENVOY_LOG(debug, "Got kevent: ident={}, filter={}, flags={}, fflags={}", event.ident,
              event.filter, event.flags, event.fflags);

    if (event.ident == user_event_ident_ && event.filter == EVFILT_USER) {
      ENVOY_LOG(info, "kqueue user event detected - trigger consumed!");
      return true;
    } else {
      ENVOY_LOG(debug, "kevent not matching our user event (expected ident={}, filter={})",
                user_event_ident_, EVFILT_USER);
    }
  }

  return false;
}

void KqueueUserTrigger::reset() {
  // User events are automatically cleared with EV_CLEAR flag
  ENVOY_LOG(debug, "kqueue user event reset (automatic with EV_CLEAR)");
}
#endif

#ifdef __linux__
// Linux eventfd implementation
EventfdTrigger::~EventfdTrigger() {
  if (eventfd_ != -1) {
    ::close(eventfd_);
  }
}

bool EventfdTrigger::initialize(Event::Dispatcher& dispatcher) {
  (void)dispatcher; // Unused - Envoy listener monitors FD directly
  // Create eventfd with close-on-exec flag
  eventfd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (eventfd_ == -1) {
    ENVOY_LOG(error, "Failed to create eventfd: {}", strerror(errno));
    return false;
  }

  // NOTE: We don't register file events here because Envoy's listener
  // will monitor our eventfd directly via fdDoNotUse() override

  ENVOY_LOG(debug, "Initialized eventfd trigger with FD: {}", eventfd_);
  return true;
}

bool EventfdTrigger::trigger() {
  if (eventfd_ == -1) {
    ENVOY_LOG(error, "eventfd not initialized");
    return false;
  }

  // Write to eventfd to trigger it
  uint64_t value = 1;
  ssize_t result = ::write(eventfd_, &value, sizeof(value));
  if (result != sizeof(value)) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ENVOY_LOG(error, "Failed to write to eventfd: {}", strerror(errno));
      return false;
    }
    // EAGAIN means eventfd counter is at maximum, which is fine
  }

  ENVOY_LOG(debug, "Successfully triggered eventfd");
  return true;
}

bool EventfdTrigger::wait() {
  if (eventfd_ == -1) {
    return false;
  }

  // Read from eventfd to check if triggered
  uint64_t value;
  ssize_t result = ::read(eventfd_, &value, sizeof(value));
  if (result == sizeof(value)) {
    ENVOY_LOG(debug, "eventfd triggered with value: {}", value);
    return true;
  }

  if (result == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
    ENVOY_LOG(error, "Failed to read from eventfd: {}", strerror(errno));
  }

  return false;
}

void EventfdTrigger::reset() {
  // eventfd is automatically reset when read
  ENVOY_LOG(debug, "eventfd reset (automatic on read)");
}
#endif

// Fallback pipe implementation
PipeTrigger::~PipeTrigger() {
  if (read_fd_ != -1) {
    ::close(read_fd_);
  }
  if (write_fd_ != -1) {
    ::close(write_fd_);
  }
}

bool PipeTrigger::initialize(Event::Dispatcher& dispatcher) {
  (void)dispatcher; // Unused - Envoy listener monitors FD directly
  // Create pipe
  int pipe_fds[2];
  if (::pipe(pipe_fds) == -1) {
    ENVOY_LOG(error, "Failed to create pipe: {}", strerror(errno));
    return false;
  }

  read_fd_ = pipe_fds[0];
  write_fd_ = pipe_fds[1];

  // Make both ends non-blocking
  int flags = ::fcntl(read_fd_, F_GETFL, 0);
  if (flags != -1) {
    ::fcntl(read_fd_, F_SETFL, flags | O_NONBLOCK);
  }

  flags = ::fcntl(write_fd_, F_GETFL, 0);
  if (flags != -1) {
    ::fcntl(write_fd_, F_SETFL, flags | O_NONBLOCK);
  }

  // NOTE: We don't register file events here because Envoy's listener
  // will monitor our pipe read FD directly via fdDoNotUse() override

  ENVOY_LOG(debug, "Initialized pipe trigger with read FD: {}, write FD: {}", read_fd_, write_fd_);
  return true;
}

bool PipeTrigger::trigger() {
  if (write_fd_ == -1) {
    ENVOY_LOG(error, "pipe not initialized");
    return false;
  }

  // Write single byte to pipe
  char trigger_byte = 1;
  ssize_t result = ::write(write_fd_, &trigger_byte, 1);
  if (result != 1) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ENVOY_LOG(error, "Failed to write to pipe: {}", strerror(errno));
      return false;
    }
    // EAGAIN means pipe buffer is full, which is fine for trigger purposes
  }

  ENVOY_LOG(debug, "Successfully triggered pipe");
  return true;
}

bool PipeTrigger::wait() {
  if (read_fd_ == -1) {
    return false;
  }

  // Read from pipe to check if triggered
  char buffer[64]; // Read multiple bytes if available
  ssize_t result = ::read(read_fd_, buffer, sizeof(buffer));
  if (result > 0) {
    ENVOY_LOG(debug, "pipe triggered, read {} bytes", result);
    return true;
  }

  if (result == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
    ENVOY_LOG(error, "Failed to read from pipe: {}", strerror(errno));
  }

  return false;
}

void PipeTrigger::reset() {
  // Pipe is reset by reading from it in wait()
  ENVOY_LOG(debug, "pipe reset (via read in wait())");
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
