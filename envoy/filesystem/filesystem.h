#pragma once

#include <bitset>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/api/io_error.h"
#include "envoy/common/platform.h"
#include "envoy/common/pure.h"
#include "envoy/common/time.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Filesystem {

using FlagSet = std::bitset<5>;

enum class DestinationType { File, Stderr, Stdout, TmpFile };

enum class FileType { Regular, Directory, Other };

struct FileInfo {
  const std::string name_;
  // the size of the file in bytes, or `nullopt` if the size could not be determined
  //         (e.g. for directories, or windows symlinks.)
  const absl::optional<uint64_t> size_;
  // Note that if the file represented by name_ is a symlink, type_ will be the file type of the
  // target. For example, if name_ is a symlink to a directory, its file type will be Directory.
  // A broken symlink on posix will have `FileType::Regular`.
  const FileType file_type_;
  const absl::optional<SystemTime> time_created_;
  const absl::optional<SystemTime> time_last_accessed_;
  const absl::optional<SystemTime> time_last_modified_;
};

/**
 * Abstraction for a basic file on disk.
 */
class File {
public:
  virtual ~File() = default;

  enum Operation {
    // Open a file for reading.
    Read,
    // Open a file for writing. The file will be truncated if neither Append nor
    // KeepExisting is set.
    Write,
    // Create the file if it does not already exist
    Create,
    // If writing, append to the file rather than writing to the beginning and
    // truncating after write.
    Append,
    // To open for write, without appending, and without truncating, add the
    // KeepExistingData flag. It is especially important to set this flag if
    // using pwrite, as the Windows implementation of truncation will interact
    // poorly with pwrite.
    KeepExistingData,
  };

  /**
   * Open the file with Flag
   * The file will be closed when this object is destructed
   *
   * @return bool whether the open succeeded
   */
  virtual Api::IoCallBoolResult open(FlagSet flags) PURE;

  /**
   * Write the buffer to the file. The file must be explicitly opened before writing.
   *
   * @return ssize_t number of bytes written, or -1 for failure
   */
  virtual Api::IoCallSizeResult write(absl::string_view buffer) PURE;

  /**
   * Get additional details about the file. May or may not require a file system operation.
   *
   * @return FileInfo the file info.
   */
  virtual Api::IoCallResult<FileInfo> info() PURE;

  /**
   * Close the file.
   *
   * @return bool whether the close succeeded
   */
  virtual Api::IoCallBoolResult close() PURE;

  /**
   * Read a chunk of data from the file to a buffer. The file must be explicitly opened
   * before reading.
   * @param buf The buffer to copy the data into.
   * @param count The maximum number of bytes to read.
   * @param offset The offset in the file at which to start reading.
   * @return ssize_t number of bytes read, or -1 for failure.
   */
  virtual Api::IoCallSizeResult pread(void* buf, uint64_t count, uint64_t offset) PURE;

  /**
   * Write a chunk of data from a buffer to the file. The file must be explicitly opened
   * before writing.
   * @param buf The buffer to read the data from.
   * @param count The maximum number of bytes to write.
   * @param offset The offset in the file at which to start writing.
   * @return ssize_t number of bytes written, or -1 for failure.
   */
  virtual Api::IoCallSizeResult pwrite(const void* buf, uint64_t count, uint64_t offset) PURE;

  /**
   * @return bool is the file open
   */
  virtual bool isOpen() const PURE;

  /**
   * @return string the file path
   */
  virtual std::string path() const PURE;

  /**
   * @return the type of the destination
   */
  virtual DestinationType destinationType() const PURE;
};

using FilePtr = std::unique_ptr<File>;

/**
 * Contains the result of splitting the file name and its parent directory from
 * a given file path.
 */
struct PathSplitResult {
  absl::string_view directory_;
  absl::string_view file_;
};

/**
 * Contains the file type and the path.
 */
struct FilePathAndType {
  bool operator==(const FilePathAndType& rhs) const {
    return file_type_ == rhs.file_type_ && path_ == rhs.path_;
  }
  FilePathAndType() = default;
  FilePathAndType(DestinationType file_type, absl::string_view path)
      : file_type_(file_type), path_(path) {}
  DestinationType file_type_;
  std::string path_;
};

/**
 * Abstraction for some basic filesystem operations
 */
class Instance {
public:
  virtual ~Instance() = default;

  /**
   *  @param file_info The path and the type of the File
   *  @return a FilePtr. The file is not opened.
   */
  virtual FilePtr createFile(const FilePathAndType& file_info) PURE;

  /**
   * @return bool whether a file exists on disk and can be opened for read.
   */
  virtual bool fileExists(const std::string& path) PURE;

  /**
   * @return FileInfo containing information about the file, or an error status.
   */
  virtual Api::IoCallResult<FileInfo> stat(absl::string_view path) PURE;

  /**
   * Attempts to create the given path, recursively if necessary.
   * @return bool true if one or more directories was created and the path exists,
   *         false if the path already existed, an error status if the path does
   *         not exist after the call.
   */
  virtual Api::IoCallBoolResult createPath(absl::string_view path) PURE;

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
   * @return full file content as a string or an error if the file can not be read.
   * Be aware, this is not most highly performing file reading method.
   */
  virtual absl::StatusOr<std::string> fileReadToEnd(const std::string& path) PURE;

  /**
   * @path file path to split
   * @return PathSplitResult containing the parent directory of the input path and the file name or
   * an error status.
   */
  virtual absl::StatusOr<PathSplitResult> splitPathFromFilename(absl::string_view path) PURE;

  /**
   * Determine if the path is on a list of paths Envoy will refuse to access. This
   * is a basic sanity check for users, denying some clearly bad paths. Paths
   * may still be problematic (e.g. indirectly leading to /dev/mem) even if this
   * returns false, it is up to the user to validate that supplied paths are
   * valid.
   * @param path some filesystem path.
   * @return is the path on the deny list?
   */
  virtual bool illegalPath(const std::string& path) PURE;
};

using InstancePtr = std::unique_ptr<Instance>;

struct DirectoryEntry {
  // name_ is the name of the file in the directory, not including the directory path itself
  // For example, if we have directory a/b containing file c, name_ will be c
  std::string name_;

  // Note that if the file represented by name_ is a symlink, type_ will be the file type of the
  // target. For example, if name_ is a symlink to a directory, its file type will be Directory.
  FileType type_;

  // The file size in bytes for regular files. nullopt for FileType::Directory and FileType::Other,
  // and, on Windows, also nullopt for symlinks, and on Linux nullopt for broken symlinks.
  absl::optional<uint64_t> size_bytes_;

  bool operator==(const DirectoryEntry& rhs) const {
    return name_ == rhs.name_ && type_ == rhs.type_ && size_bytes_ == rhs.size_bytes_;
  }
};

class DirectoryIteratorImpl;

// Failures during this iteration will be silent; check status() after initialization
// and after each increment, if error-handling is desired.
class DirectoryIterator {
public:
  DirectoryIterator() : entry_({"", FileType::Other, absl::nullopt}) {}
  virtual ~DirectoryIterator() = default;

  const DirectoryEntry& operator*() const { return entry_; }

  bool operator!=(const DirectoryIterator& rhs) const { return !(entry_ == *rhs); }

  virtual DirectoryIteratorImpl& operator++() PURE;

  const absl::Status& status() const { return status_; }

protected:
  DirectoryEntry entry_;
  absl::Status status_;
};

} // namespace Filesystem
} // namespace Envoy
