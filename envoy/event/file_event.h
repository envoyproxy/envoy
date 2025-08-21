#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Event {

struct FileReadyType {
  // File is ready for reading.
  static constexpr uint32_t Read = 0x1;
  // File is ready for writing.
  static constexpr uint32_t Write = 0x2;
  // File has been remote closed.
  static constexpr uint32_t Closed = 0x4;
};

enum class FileTriggerType {
  // See @man 7 epoll(7)
  // They are used on all platforms for DNS and TCP listeners.
  Level,
  // See @man 7 epoll(7)
  // They are used on all platforms that support Edge triggering as the default trigger type.
  Edge,
  // These are synthetic edge events managed by Envoy. They are based on level events and when they
  // are activated they are immediately disabled. This makes them behave like Edge events. Then it
  // is is the responsibility of the consumer of the event to reactivate the event
  // when the socket operation would block.
  //
  // Their main application in Envoy is for Win32 which does not support edge-triggered events. They
  // should be used in Win32 instead of level events. They can only be used in platforms where
  // `PlatformDefaultTriggerType` is `FileTriggerType::EmulatedEdge`.
  EmulatedEdge
};

// For POSIX developers to get the Windows behavior of file events
// you need to add the following definition:
// `FORCE_LEVEL_EVENTS`
// You can do this with bazel if you add the following build/test options
// `--copt="-DFORCE_LEVEL_EVENTS"`
constexpr FileTriggerType determinePlatformPreferredEventType() {
#if defined(WIN32) || defined(FORCE_LEVEL_EVENTS)
  return FileTriggerType::EmulatedEdge;
#else
  return FileTriggerType::Edge;
#endif
}

static constexpr FileTriggerType PlatformDefaultTriggerType = determinePlatformPreferredEventType();

/**
 * Callback invoked when a FileEvent is ready for reading or writing.
 */
using FileReadyCb = std::function<absl::Status(uint32_t events)>;

/**
 * Wrapper for file based (read/write) event notifications.
 */
class FileEvent {
public:
  virtual ~FileEvent() = default;

  /**
   * Activate the file event explicitly for a set of events. Should be a logical OR of FileReadyType
   * events. This method "injects" the event (and fires callbacks) regardless of whether the event
   * is actually ready on the underlying file.
   */
  virtual void activate(uint32_t events) PURE;

  /**
   * Enable the file event explicitly for a set of events. Should be a logical OR of FileReadyType
   * events. As opposed to activate(), this routine causes the file event to listen for the
   * registered events and fire callbacks when they are active.
   */
  virtual void setEnabled(uint32_t events) PURE;

  /**
   * Add a single event from the event registration mark.
   */
  virtual void registerEventIfEmulatedEdge(uint32_t event) PURE;

  /**
   * Remove a single event from the event registration mark.
   */
  virtual void unregisterEventIfEmulatedEdge(uint32_t event) PURE;
};

using FileEventPtr = std::unique_ptr<FileEvent>;

} // namespace Event
} // namespace Envoy
