#include "common/access_log/access_log_manager_impl.h"

#include <string>

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/lock_guard.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace AccessLog {

void AccessLogManagerImpl::reopen() {
  for (auto& access_log : access_logs_) {
    access_log.second->reopen();
  }
}

AccessLogFileSharedPtr AccessLogManagerImpl::createAccessLog(const std::string& file_name_arg) {
  const std::string* file_name = &file_name_arg;
#ifdef WIN32
  // Preserve the expected behavior of specifying path: /dev/null on Windows
  static const std::string windows_dev_null("NUL");
  if (file_name_arg.compare("/dev/null") == 0) {
    file_name = static_cast<const std::string*>(&windows_dev_null);
  }
#endif

  std::unordered_map<std::string, AccessLogFileSharedPtr>::const_iterator access_log =
      access_logs_.find(*file_name);
  if (access_log != access_logs_.end()) {
    return access_log->second;
  }

  access_logs_[*file_name] = std::make_shared<AccessLogFileImpl>(
      api_.fileSystem().createFile(*file_name), dispatcher_, lock_, file_stats_,
      file_flush_interval_msec_, api_.threadFactory());
  return access_logs_[*file_name];
}

AccessLogFileImpl::AccessLogFileImpl(Filesystem::FilePtr&& file, Event::Dispatcher& dispatcher,
                                     Thread::BasicLockable& lock, AccessLogFileStats& stats,
                                     std::chrono::milliseconds flush_interval_msec,
                                     Thread::ThreadFactory& thread_factory)
    : file_(std::move(file)), file_lock_(lock),
      flush_timer_(dispatcher.createTimer([this]() -> void {
        stats_.flushed_by_timer_.inc();
        flush_event_.notifyOne();
        flush_timer_->enableTimer(flush_interval_msec_);
      })),
      thread_factory_(thread_factory), flush_interval_msec_(flush_interval_msec), stats_(stats) {
  open();
}

Filesystem::FlagSet AccessLogFileImpl::defaultFlags() {
  static constexpr Filesystem::FlagSet default_flags{1 << Filesystem::File::Operation::Write |
                                                     1 << Filesystem::File::Operation::Create |
                                                     1 << Filesystem::File::Operation::Append};

  return default_flags;
}

void AccessLogFileImpl::open() {
  const Api::IoCallBoolResult result = file_->open(defaultFlags());
  if (!result.rc_) {
    throw EnvoyException(
        fmt::format("unable to open file '{}': {}", file_->path(), result.err_->getErrorDetails()));
  }
}

void AccessLogFileImpl::reopen() { reopen_file_ = true; }

AccessLogFileImpl::~AccessLogFileImpl() {
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

    const Api::IoCallBoolResult result = file_->close();
    ASSERT(result.rc_, fmt::format("unable to close file '{}': {}", file_->path(),
                                   result.err_->getErrorDetails()));
  }
}

void AccessLogFileImpl::doWrite(Buffer::Instance& buffer) {
  Buffer::RawSliceVector slices = buffer.getRawSlices();

  // We must do the actual writes to disk under lock, so that we don't intermix chunks from
  // different AccessLogFileImpl pointing to the same underlying file. This can happen either via
  // hot restart or if calling code opens the same underlying file into a different
  // AccessLogFileImpl in the same process.
  // TODO PERF: Currently, we use a single cross process lock to serialize all disk writes. This
  //            will never block network workers, but does mean that only a single flush thread can
  //            actually flush to disk. In the future it would be nice if we did away with the cross
  //            process lock or had multiple locks.
  {
    Thread::LockGuard lock(file_lock_);
    for (const Buffer::RawSlice& slice : slices) {
      absl::string_view data(static_cast<char*>(slice.mem_), slice.len_);
      const Api::IoCallSizeResult result = file_->write(data);
      if (result.ok() && result.rc_ == static_cast<ssize_t>(slice.len_)) {
        stats_.write_completed_.inc();
      } else {
        // Probably disk full.
        stats_.write_failed_.inc();
      }
    }
  }

  stats_.write_total_buffered_.sub(buffer.length());
  buffer.drain(buffer.length());
}

void AccessLogFileImpl::flushThreadFunc() {

  while (true) {
    std::unique_lock<Thread::BasicLockable> flush_lock;

    {
      Thread::LockGuard write_lock(write_lock_);

      // flush_event_ can be woken up either by large enough flush_buffer or by timer.
      // In case it was timer, flush_buffer_ can be empty.
      while (flush_buffer_.length() == 0 && !flush_thread_exit_ && !reopen_file_) {
        // CondVar::wait() does not throw, so it's safe to pass the mutex rather than the guard.
        flush_event_.wait(write_lock_);
      }

      if (flush_thread_exit_) {
        return;
      }

      flush_lock = std::unique_lock<Thread::BasicLockable>(flush_lock_);
      about_to_write_buffer_.move(flush_buffer_);
      ASSERT(flush_buffer_.length() == 0);
    }

    // if we failed to open file before, then simply ignore
    if (file_->isOpen()) {
      try {
        if (reopen_file_) {
          reopen_file_ = false;
          const Api::IoCallBoolResult result = file_->close();
          ASSERT(result.rc_, fmt::format("unable to close file '{}': {}", file_->path(),
                                         result.err_->getErrorDetails()));
          open();
        }

        doWrite(about_to_write_buffer_);
      } catch (const EnvoyException&) {
        stats_.reopen_failed_.inc();
      }
    }
  }
}

void AccessLogFileImpl::flush() {
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

void AccessLogFileImpl::write(absl::string_view data) {
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

void AccessLogFileImpl::createFlushStructures() {
  flush_thread_ = thread_factory_.createThread([this]() -> void { flushThreadFunc(); });
  flush_timer_->enableTimer(flush_interval_msec_);
}

} // namespace AccessLog
} // namespace Envoy
