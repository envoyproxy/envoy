#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Filesystem {

class OsSysCalls {
public:
  virtual ~OsSysCalls(){};

  /**
   * Open file by full_path with given flags and mode.
   * @return file descriptor.
   */
  virtual int open(const std::string& full_path, int flags, int mode) PURE;

  /**
   * Write num_bytes to fd from buffer.
   * @return number of bytes written if non negative, otherwise error code.
   */
  virtual ssize_t write(int fd, const void* buffer, size_t num_bytes) PURE;

  /**
   * Release all resources allocated for fd.
   * @return zero on success, -1 returned otherwise.
   */
  virtual int close(int fd) PURE;
};

typedef std::unique_ptr<OsSysCalls> OsSysCallsPtr;

/**
 * Abstraction for a file on disk.
 */
class File {
public:
  virtual ~File() {}

  /**
   * Write data to the file.
   */
  virtual void write(const std::string& data) PURE;

  /**
   * Reopen the file.
   */
  virtual void reopen() PURE;
};

typedef std::shared_ptr<File> FileSharedPtr;

/**
 * Abstraction for a file watcher.
 */
class Watcher {
public:
  typedef std::function<void(uint32_t events)> OnChangedCb;

  struct Events {
    static const uint32_t MovedTo = 0x1;
  };

  virtual ~Watcher() {}

  /**
   * Add a file watch.
   * @param path supplies the path to watch.
   * @param events supplies the events to watch.
   * @param cb supplies the callback to invoke when a change occurs.
   */
  virtual void addWatch(const std::string& path, uint32_t events, OnChangedCb cb) PURE;
};

typedef std::unique_ptr<Watcher> WatcherPtr;

} // namespace Filesystem
} // namespace Envoy
