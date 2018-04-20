#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>

#include "envoy/api/os_sys_calls.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/thread.h"

namespace Envoy {
// clang-format off
#define FILESYSTEM_STATS(COUNTER, GAUGE)                                                           \
  COUNTER(write_buffered)                                                                          \
  COUNTER(write_completed)                                                                         \
  COUNTER(flushed_by_timer)                                                                        \
  COUNTER(reopen_failed)                                                                           \
  GAUGE  (write_total_buffered)
// clang-format on

struct FileSystemStats {
  FILESYSTEM_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

namespace Filesystem {

/**
 * @return bool whether a file exists on disk and can be opened for read.
 */
bool fileExists(const std::string& path);

/**
 * @return bool whether a directory exists on disk and can be opened for read.
 */
bool directoryExists(const std::string& path);

/**
 * @return ssize_t the size in bytes of the specified file, or -1 if the file size
 *                 cannot be determined for any reason, including without limitation
 *                 the non-existence of the file.
 */
ssize_t fileSize(const std::string& path);

/**
 * @return full file content as a string.
 * @throw EnvoyException if the file cannot be read.
 * Be aware, this is not most highly performing file reading method.
 */
std::string fileReadToEnd(const std::string& path);

/**
 * This is a file implementation geared for writing out access logs. It turn out that in certain
 * cases even if a standard file is opened with O_NONBLOCK, the kernel can still block when writing.
 * This implementation uses a flush thread per file, with the idea there there aren't that many
 * files. If this turns out to be a good implementation we can potentially have a single flush
 * thread that flushes all files, but we will start with this.
 */
class FileImpl : public File {
public:
  FileImpl(const std::string& path, Event::Dispatcher& dispatcher, Thread::BasicLockable& lock,
           Stats::Store& stats_store, std::chrono::milliseconds flush_interval_msec);
  ~FileImpl();

  // Filesystem::File
  void write(absl::string_view data) override;

  /**
   * Filesystem::File
   * Reopen file asynchronously.
   * This only sets reopen flag, actual reopen operation is delayed.
   * Reopen happens before the next write operation.
   */
  void reopen() override;

  // Fileystem::File
  void flush() override;

private:
  void doWrite(Buffer::Instance& buffer);
  void flushThreadFunc();
  void open();
  void createFlushStructures();

  // Minimum size before the flush thread will be told to flush.
  static const uint64_t MIN_FLUSH_SIZE = 1024 * 64;

  int fd_;
  std::string path_;

  // These locks are always acquired in the following order if multiple locks are held:
  //    1) write_lock_
  //    2) flush_lock_
  //    3) file_lock_
  Thread::BasicLockable& file_lock_; // This lock is used only by the flush thread when writing
                                     // to disk. This is used to make sure that file blocks do
                                     // not get interleaved by multiple processes writing to
                                     // the same file during hot-restart.
  std::mutex flush_lock_;            // This lock is used to prevent simulataneous flushes from
                                     // the flush thread and a syncronous flush. This protects
                                     // concurrent access to the about_to_write_buffer_, fd_,
                                     // and all other data used during flushing and file
                                     // re-opening.
  std::mutex write_lock_;            // The lock is used when filling the flush buffer. It allows
                                     // multiple threads to write to the same file at relatively
                                     // high performance. It is always local to the process.
  Thread::ThreadPtr flush_thread_;
  std::condition_variable_any flush_event_;
  std::atomic<bool> flush_thread_exit_{};
  std::atomic<bool> reopen_file_{};
  Buffer::OwnedImpl flush_buffer_; // This buffer is used by multiple threads. It gets filled and
                                   // then flushed either when max size is reached or when a timer
                                   // fires.
  Buffer::OwnedImpl about_to_write_buffer_; // This buffer is used only by the flush thread. Data
                                            // is moved from flush_buffer_ under lock, and then
                                            // the lock is released so that flush_buffer_ can
                                            // continue to fill. This buffer is then used for the
                                            // final write to disk.
  Event::TimerPtr flush_timer_;
  Api::OsSysCalls& os_sys_calls_;
  const std::chrono::milliseconds flush_interval_msec_; // Time interval buffer gets flushed no
                                                        // matter if it reached the MIN_FLUSH_SIZE
                                                        // or not.
  FileSystemStats stats_;
};

} // namespace Filesystem
} // Envoy
