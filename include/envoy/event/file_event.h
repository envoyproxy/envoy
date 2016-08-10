#pragma once

namespace Event {

/**
 * Callback invoked when a FileEvent is ready for reading or writing.
 */
typedef std::function<void()> FileReadyCb;

/**
 * Wrapper for file based (read/write) event notifications.
 */
class FileEvent {
public:
  virtual ~FileEvent() {}
};

typedef std::unique_ptr<FileEvent> FileEventPtr;

} // Event
