#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Event {

struct FileReadyType {
  // File is ready for reading.
  static const uint32_t Read = 0x1;
  // File is ready for writing.
  static const uint32_t Write = 0x2;
  // File has been remote closed.
  static const uint32_t Closed = 0x4;
};

#define FORCE_LEVEL_EVENTS 0
enum class FileTriggerType { Level, Edge, EmulatedEdge };

static constexpr bool optimizeLevelEvents = true;

constexpr FileTriggerType isPlatformUsingLevelEvents() {
#if defined(WIN32) || defined(FORCE_LEVEL_EVENTS)
  if constexpr (optimizeLevelEvents) {
    return FileTriggerType::EmulatedEdge;
  } else {
    return FileTriggerType::Level;
  }
#else
  return FileTriggerType::Edge;
#endif
}

static constexpr FileTriggerType PlatformDefaultTriggerType = isPlatformUsingLevelEvents();

static constexpr bool isLevelLike(FileTriggerType event) {
  return (event == FileTriggerType::EmulatedEdge) || (event == FileTriggerType::Level);
}

/**
 * Callback invoked when a FileEvent is ready for reading or writing.
 */
using FileReadyCb = std::function<void(uint32_t events)>;

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
  virtual void registerReadOrWriteIfLevel(uint32_t event) PURE;

  /**
   * Remove a single event from the event registration mark.
   */
  virtual void unregisterReadOrWriteIfLevel(uint32_t event) PURE;
};

using FileEventPtr = std::unique_ptr<FileEvent>;

} // namespace Event
} // namespace Envoy
