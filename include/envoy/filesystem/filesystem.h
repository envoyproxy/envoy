#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Filesystem {

/**
 * Abstraction for a file on disk.
 */
class File {
public:
  virtual ~File() {}

  /**
   * Write data to the file.
   */
  virtual void write(absl::string_view) PURE;

  /**
   * Reopen the file.
   */
  virtual void reopen() PURE;

  /**
   * Synchronously flush all pending data to disk.
   */
  virtual void flush() PURE;
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

/**
 * Abstraction for listing a directory.
 * TODO(sesmith177): replace with std::filesystem::directory_iterator once we move to C++17
 */

class DirectoryIterator {
public:
  enum class FileType { Regular, Directory, Other };

  typedef struct {
    std::string path_;
    FileType type_;
  } DirectoryEntry;

  DirectoryIterator(const std::string& directory_path) : directory_path_(directory_path) {}
  virtual ~DirectoryIterator() {}

  virtual DirectoryEntry nextEntry() PURE;

protected:
  std::string directory_path_;
};

typedef std::unique_ptr<DirectoryIterator> DirectoryIteratorPtr;

} // namespace Filesystem
} // namespace Envoy
