#pragma once

#include "envoy/common/pure.h"

namespace Event {

struct FileReadyType {
  static const uint32_t Read = 0x1;
  static const uint32_t Write = 0x2;
};

/**
 * Callback invoked when a FileEvent is ready for reading or writing.
 */
typedef std::function<void(uint32_t events)> FileReadyCb;

/**
 * Wrapper for file based (read/write) event notifications.
 */
class FileEvent {
public:
  virtual ~FileEvent() {}

  /**
   * Activate the file event explicitly for a set of events. Should be a logical OR of FileReadyType
   * events.
   */
  virtual void activate(uint32_t events) PURE;
};

typedef std::unique_ptr<FileEvent> FileEventPtr;

} // Event
