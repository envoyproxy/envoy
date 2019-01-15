#include "common/filesystem/stats_instance_impl.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/thread/thread.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/lock_guard.h"
#include "common/common/stack_array.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Filesystem {

StatsInstanceImpl::StatsInstanceImpl(std::chrono::milliseconds file_flush_interval_msec,
                                     Thread::ThreadFactory& thread_factory,
                                     Stats::Store& stats_store, Instance& file_system)
    : file_flush_interval_msec_(file_flush_interval_msec),
      file_stats_{FILESYSTEM_STATS(POOL_COUNTER_PREFIX(stats_store, "filesystem."),
                                   POOL_GAUGE_PREFIX(stats_store, "filesystem."))},
      thread_factory_(thread_factory), file_system_(file_system) {}

StatsFileSharedPtr
StatsInstanceImpl::createStatsFile(const std::string& path, Event::Dispatcher& dispatcher,
                                   Thread::BasicLockable& lock,
                                   std::chrono::milliseconds file_flush_interval_msec) {
  return std::make_shared<StatsFileImpl>(path, dispatcher, lock, file_stats_,
                                         file_flush_interval_msec, thread_factory_, file_system_);
};

StatsFileSharedPtr StatsInstanceImpl::createStatsFile(const std::string& path,
                                                      Event::Dispatcher& dispatcher,
                                                      Thread::BasicLockable& lock) {
  return createStatsFile(path, dispatcher, lock, file_flush_interval_msec_);
}

bool StatsInstanceImpl::fileExists(const std::string& path) {
  return file_system_.fileExists(path);
}

bool StatsInstanceImpl::directoryExists(const std::string& path) {
  return file_system_.directoryExists(path);
}

ssize_t StatsInstanceImpl::fileSize(const std::string& path) { return file_system_.fileSize(path); }

std::string StatsInstanceImpl::fileReadToEnd(const std::string& path) {
  return file_system_.fileReadToEnd(path);
}

bool StatsInstanceImpl::illegalPath(const std::string& path) {
  return file_system_.illegalPath(path);
}

FilePtr StatsInstanceImpl::createFile(const std::string& path) {
  return file_system_.createFile(path);
}

StatsFileImpl::StatsFileImpl(const std::string& path, Event::Dispatcher& dispatcher,
                             Thread::BasicLockable& lock, FileSystemStats& stats,
                             std::chrono::milliseconds flush_interval_msec,
                             Thread::ThreadFactory& thread_factory,
                             Filesystem::Instance& file_system)
    : file_lock_(lock), flush_timer_(dispatcher.createTimer([this]() -> void {
        stats_.flushed_by_timer_.inc();
        flush_event_.notifyOne();
        flush_timer_->enableTimer(flush_interval_msec_);
      })),
      thread_factory_(thread_factory), flush_interval_msec_(flush_interval_msec), stats_(stats),
      file_(file_system.createFile(path)) {}

void StatsFileImpl::reopen() { reopen_file_ = true; }

StatsFileImpl::~StatsFileImpl() {
  {
    Thread::LockGuard lock(write_lock_);
    flush_thread_exit_ = true;
    flush_event_.notifyOne();
  }

  if (flush_thread_ != nullptr) {
    flush_thread_->join();
  }

  // Flush any remaining data. If file was not opened for some reason, skip flushing part.
  if (file_->isOpen()) {
    if (flush_buffer_.length() > 0) {
      doWrite(flush_buffer_);
    }

    file_->close();
  }
}

void StatsFileImpl::doWrite(Buffer::Instance& buffer) {
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);

  // We must do the actual writes to disk under lock, so that we don't intermix chunks from
  // different StatsFileImpl pointing to the same underlying file. This can happen either via hot
  // restart or if calling code opens the same underlying file into a different StatsFileImpl in the
  // same process.
  // TODO PERF: Currently, we use a single cross process lock to serialize all disk writes. This
  //            will never block network workers, but does mean that only a single flush thread can
  //            actually flush to disk. In the future it would be nice if we did away with the cross
  //            process lock or had multiple locks.
  {
    Thread::LockGuard lock(file_lock_);
    for (const Buffer::RawSlice& slice : slices) {
      const Api::SysCallSizeResult result = file_->write(slice.mem_, slice.len_);
      ASSERT(result.rc_ == static_cast<ssize_t>(slice.len_));
      stats_.write_completed_.inc();
    }
  }

  stats_.write_total_buffered_.sub(buffer.length());
  buffer.drain(buffer.length());
}

void StatsFileImpl::flushThreadFunc() {

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

    // if we failed to open file before then simply ignore
    if (file_->isOpen()) {
      try {
        if (reopen_file_) {
          reopen_file_ = false;
          file_->close();
          file_->open();
        }

        doWrite(about_to_write_buffer_);
      } catch (const EnvoyException&) {
        stats_.reopen_failed_.inc();
      }
    }
  }
}

void StatsFileImpl::flush() {
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

void StatsFileImpl::write(absl::string_view data) {
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

void StatsFileImpl::createFlushStructures() {
  flush_thread_ = thread_factory_.createThread([this]() -> void { flushThreadFunc(); });
  flush_timer_->enableTimer(flush_interval_msec_);
}

} // namespace Filesystem
} // namespace Envoy
