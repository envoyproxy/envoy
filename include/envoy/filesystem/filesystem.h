#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/thread/thread.h"

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
 * Captures state, properties, and stats of a file-system.
 */
class Instance {
public:
  virtual ~Instance() {}

  /**
   * Creates a file, overriding the flush-interval set in the class.
   *
   * @param path The path of the file to open.
   * @param dispatcher The dispatcher used for set up timers to run flush().
   * @param lock The lock.
   * @param file_flush_interval_msec Number of milliseconds to delay before flushing.
   */
  virtual FileSharedPtr createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                   Thread::BasicLockable& lock,
                                   std::chrono::milliseconds file_flush_interval_msec) PURE;

  /**
   * Creates a file, using the default flush-interval for the class.
   *
   * @param path The path of the file to open.
   * @param dispatcher The dispatcher used for set up timers to run flush().
   * @param lock The lock.
   */
  virtual FileSharedPtr createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                   Thread::BasicLockable& lock) PURE;

  /**
   * @return bool whether a file exists on disk and can be opened for read.
   */
  virtual bool fileExists(const std::string& path) PURE;

  /**
   * @return bool whether a directory exists on disk and can be opened for read.
   */
  virtual bool directoryExists(const std::string& path) PURE;

  /**
   * @return ssize_t the size in bytes of the specified file, or -1 if the file size
   *                 cannot be determined for any reason, including without limitation
   *                 the non-existence of the file.
   */
  virtual ssize_t fileSize(const std::string& path) PURE;

  /**
   * @return full file content as a string.
   * @throw EnvoyException if the file cannot be read.
   * Be aware, this is not most highly performing file reading method.
   */
  virtual std::string fileReadToEnd(const std::string& path) PURE;

  /**
   * @param path some filesystem path.
   * @return SysCallStringResult containing the canonical path (see realpath(3)).
   */
  virtual Api::SysCallStringResult canonicalPath(const std::string& path) PURE;

  /**
   * Determine if the path is on a list of paths Envoy will refuse to access. This
   * is a basic sanity check for users, blacklisting some clearly bad paths. Paths
   * may still be problematic (e.g. indirectly leading to /dev/mem) even if this
   * returns false, it is up to the user to validate that supplied paths are
   * valid.
   * @param path some filesystem path.
   * @return is the path on the blacklist?
   */
  virtual bool illegalPath(const std::string& path) PURE;
};

typedef std::unique_ptr<Watcher> WatcherPtr;

enum class FileType { Regular, Directory, Other };

struct DirectoryEntry {
  // name_ is the name of the file in the directory, not including the directory path itself
  // For example, if we have directory a/b containing file c, name_ will be c
  std::string name_;

  // Note that if the file represented by name_ is a symlink, type_ will be the file type of the
  // target. For example, if name_ is a symlink to a directory, its file type will be Directory.
  FileType type_;

  bool operator==(const DirectoryEntry& rhs) const {
    return name_ == rhs.name_ && type_ == rhs.type_;
  }
};

/**
 * Abstraction for listing a directory.
 * TODO(sesmith177): replace with std::filesystem::directory_iterator once we move to C++17
 */
class DirectoryIteratorImpl;
class DirectoryIterator {
public:
  DirectoryIterator() : entry_({"", FileType::Other}) {}
  virtual ~DirectoryIterator() {}

  const DirectoryEntry& operator*() const { return entry_; }

  bool operator!=(const DirectoryIterator& rhs) const { return !(entry_ == *rhs); }

  virtual DirectoryIteratorImpl& operator++() PURE;

protected:
  DirectoryEntry entry_;
};

} // namespace Filesystem
} // namespace Envoy
