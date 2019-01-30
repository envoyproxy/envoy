#include "common/filesystem/filesystem_impl.h"

#include "envoy/common/exception.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/thread/thread.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/lock_guard.h"
#include "common/common/stack_array.h"

namespace Envoy {
namespace Filesystem {

InstanceImpl::InstanceImpl(std::chrono::milliseconds file_flush_interval_msec,
                           Thread::ThreadFactory& thread_factory, Stats::Store& stats_store,
                           RawInstance& raw_instance)
    : file_flush_interval_msec_(file_flush_interval_msec),
      file_stats_{FILESYSTEM_STATS(POOL_COUNTER_PREFIX(stats_store, "filesystem."),
                                   POOL_GAUGE_PREFIX(stats_store, "filesystem."))},
      thread_factory_(thread_factory), raw_instance_(raw_instance) {}

FileSharedPtr InstanceImpl::createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                       Thread::BasicLockable& lock,
                                       std::chrono::milliseconds file_flush_interval_msec) {
  return std::make_shared<Filesystem::FileImpl>(createRawFile(path), dispatcher, lock, file_stats_,
                                                file_flush_interval_msec, thread_factory_);
};

FileSharedPtr InstanceImpl::createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                       Thread::BasicLockable& lock) {
  return createFile(path, dispatcher, lock, file_flush_interval_msec_);
}

RawFilePtr InstanceImpl::createRawFile(const std::string& path) {
  return raw_instance_.createRawFile(path);
}

bool InstanceImpl::fileExists(const std::string& path) { return raw_instance_.fileExists(path); }

bool InstanceImpl::directoryExists(const std::string& path) {
  return raw_instance_.directoryExists(path);
}

ssize_t InstanceImpl::fileSize(const std::string& path) { return raw_instance_.fileSize(path); }

std::string InstanceImpl::fileReadToEnd(const std::string& path) {
  return raw_instance_.fileReadToEnd(path);
}

Api::SysCallStringResult InstanceImpl::canonicalPath(const std::string& path) {
  return raw_instance_.canonicalPath(path);
}

bool InstanceImpl::illegalPath(const std::string& path) { return raw_instance_.illegalPath(path); }

FileImpl::FileImpl(RawFilePtr&& raw_file, Event::Dispatcher& dispatcher,
                   Thread::BasicLockable& lock, FileSystemStats& stats,
                   std::chrono::milliseconds flush_interval_msec,
                   Thread::ThreadFactory& thread_factory)
    : raw_file_(std::move(raw_file)), file_lock_(lock),
      flush_timer_(dispatcher.createTimer([this]() -> void {
        stats_.flushed_by_timer_.inc();
        flush_event_.notifyOne();
        flush_timer_->enableTimer(flush_interval_msec_);
      })),
      thread_factory_(thread_factory), flush_interval_msec_(flush_interval_msec), stats_(stats) {
  open();
}

void FileImpl::open() {
  const auto result = raw_file_->open();
  if (!result.rc_) {
    throw EnvoyException(
        fmt::format("unable to open file '{}': {}", raw_file_->path(), strerror(result.errno_)));
  }
}

void FileImpl::reopen() { reopen_file_ = true; }

FileImpl::~FileImpl() {
  {
    Thread::LockGuard lock(write_lock_);
    flush_thread_exit_ = true;
    flush_event_.notifyOne();
  }

  if (flush_thread_ != nullptr) {
    flush_thread_->join();
  }

  // Flush any remaining data. If file was not opened for some reason, skip flushing part.
  if (raw_file_->isOpen()) {
    if (flush_buffer_.length() > 0) {
      doWrite(flush_buffer_);
    }

    const auto result = raw_file_->close();
    ASSERT(result.rc_, fmt::format("unable to close file '{}': {}", raw_file_->path(),
                                   strerror(result.errno_)));
  }
}

void FileImpl::doWrite(Buffer::Instance& buffer) {
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);

  // We must do the actual writes to disk under lock, so that we don't intermix chunks from
  // different FileImpl pointing to the same underlying file. This can happen either via hot
  // restart or if calling code opens the same underlying file into a different FileImpl in the
  // same process.
  // TODO PERF: Currently, we use a single cross process lock to serialize all disk writes. This
  //            will never block network workers, but does mean that only a single flush thread can
  //            actually flush to disk. In the future it would be nice if we did away with the cross
  //            process lock or had multiple locks.
  {
    Thread::LockGuard lock(file_lock_);
    for (const Buffer::RawSlice& slice : slices) {
      absl::string_view data(static_cast<char*>(slice.mem_), slice.len_);
      const Api::SysCallSizeResult result = raw_file_->write(data);
      ASSERT(result.rc_ == static_cast<ssize_t>(slice.len_));
      stats_.write_completed_.inc();
    }
  }

  stats_.write_total_buffered_.sub(buffer.length());
  buffer.drain(buffer.length());
}

void FileImpl::flushThreadFunc() {

  while (true) {
    std::unique_lock<Thread::BasicLockable> flush_lock;

    {
      Thread::LockGuard write_lock(write_lock_);

      // flush_event_ can be woken up either by large enough flush_buffer or by timer.
      // In case it was timer, flush_buffer_ can be empty.
      while (flush_buffer_.length() == 0 && !flush_thread_exit_) {
        // CondVar::wait() does not throw, so it's safe to pass the mutex rather than the guard.
        flush_event_.wait(write_lock_);
      }

      if (flush_thread_exit_) {
        return;
      }

      flush_lock = std::unique_lock<Thread::BasicLockable>(flush_lock_);
      ASSERT(flush_buffer_.length() > 0);
      about_to_write_buffer_.move(flush_buffer_);
      ASSERT(flush_buffer_.length() == 0);
    }

    // if we failed to open file before, then simply ignore
    if (raw_file_->isOpen()) {
      try {
        if (reopen_file_) {
          reopen_file_ = false;
          const auto result = raw_file_->close();
          ASSERT(result.rc_, fmt::format("unable to close file '{}': {}", raw_file_->path(),
                                         strerror(result.errno_)));
          open();
        }

        doWrite(about_to_write_buffer_);
      } catch (const EnvoyException&) {
        stats_.reopen_failed_.inc();
      }
    }
  }
}

void FileImpl::flush() {
  std::unique_lock<Thread::BasicLockable> flush_buffer_lock;

  {
    Thread::LockGuard write_lock(write_lock_);

    // flush_lock_ must be held while checking this or else it is
    // possible that flushThreadFunc() has already moved data from
    // flush_buffer_ to about_to_write_buffer_, has unlocked write_lock_,
    // but has not yet completed doWrite(). This would allow flush() to
    // return before the pending data has actually been written to disk.
    flush_buffer_lock = std::unique_lock<Thread::BasicLockable>(flush_lock_);

    if (flush_buffer_.length() == 0) {
      return;
    }

    about_to_write_buffer_.move(flush_buffer_);
    ASSERT(flush_buffer_.length() == 0);
  }

  doWrite(about_to_write_buffer_);
}

void FileImpl::write(absl::string_view data) {
  Thread::LockGuard lock(write_lock_);

  if (flush_thread_ == nullptr) {
    createFlushStructures();
  }

  stats_.write_buffered_.inc();
  stats_.write_total_buffered_.add(data.length());
  flush_buffer_.add(data.data(), data.size());
  if (flush_buffer_.length() > MIN_FLUSH_SIZE) {
    flush_event_.notifyOne();
  }
}

void FileImpl::createFlushStructures() {
  flush_thread_ = thread_factory_.createThread([this]() -> void { flushThreadFunc(); });
  flush_timer_->enableTimer(flush_interval_msec_);
}

} // namespace Filesystem
} // namespace Envoy
