#include "source/common/access_log/access_log_manager_impl.h"

#include <string>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/lock_guard.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace AccessLog {

AccessLogManagerImpl::~AccessLogManagerImpl() {
  for (auto& [log_key, log_file_ptr] : access_logs_) {
    ENVOY_LOG(debug, "destroying access logger {}", log_key);
    log_file_ptr.reset();
  }
  ENVOY_LOG(debug, "destroyed access loggers");
}

void AccessLogManagerImpl::reopen() {
  for (auto& iter : access_logs_) {
    iter.second->reopen();
  }
}

AccessLogFileSharedPtr
AccessLogManagerImpl::createAccessLog(const Filesystem::FilePathAndType& file_info) {
  auto file = api_.fileSystem().createFile(file_info);
  std::string file_name = file->path();
  if (access_logs_.count(file_name)) {
    return access_logs_[file_name];
  }
  access_logs_[file_name] =
      std::make_shared<AccessLogFileImpl>(std::move(file), dispatcher_, lock_, file_stats_,
                                          file_flush_interval_msec_, api_.threadFactory());
  return access_logs_[file_name];
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
  flush_timer_->enableTimer(flush_interval_msec_);
  auto open_result = open();
  if (!open_result.return_value_) {
    throwEnvoyExceptionOrPanic(fmt::format("unable to open file '{}': {}", file_->path(),
                                           open_result.err_->getErrorDetails()));
  }
}

Filesystem::FlagSet AccessLogFileImpl::defaultFlags() {
  static constexpr Filesystem::FlagSet default_flags{1 << Filesystem::File::Operation::Write |
                                                     1 << Filesystem::File::Operation::Create |
                                                     1 << Filesystem::File::Operation::Append};

  return default_flags;
}

Api::IoCallBoolResult AccessLogFileImpl::open() {
  Api::IoCallBoolResult result = file_->open(defaultFlags());
  return result;
}

void AccessLogFileImpl::reopen() {
  Thread::LockGuard lock(write_lock_);
  reopen_file_ = true;
  flush_event_.notifyOne();
}

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
    ASSERT(result.return_value_, fmt::format("unable to close file '{}': {}", file_->path(),
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
      if (result.ok() && result.return_value_ == static_cast<ssize_t>(slice.len_)) {
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

  // Transfer the action from `reopen_file_` to this variable so that `reopen_file_` is only
  // accessed while holding the mutex while the actual operation is performed while not holding the
  // mutex.
  bool do_reopen = false;

  while (true) {
    std::unique_lock<Thread::BasicLockable> flush_lock;

    {
      Thread::LockGuard write_lock(write_lock_);

      // flush_event_ can be woken up either by large enough flush_buffer or by timer.
      // In case it was timer, flush_buffer_ can be empty.
      //
      // Note: do not stop waiting when only `do_reopen` is true. In this case, we tried to
      // reopen and failed. We don't want to retry this in a tight loop, so wait for the next
      // event (timer or flush).
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

      if (reopen_file_) {
        do_reopen = true;
        reopen_file_ = false;
      }
    }

    if (do_reopen) {
      if (file_->isOpen()) {
        const Api::IoCallBoolResult result = file_->close();
        ASSERT(result.return_value_, fmt::format("unable to close file '{}': {}", file_->path(),
                                                 result.err_->getErrorDetails()));
      }
      const Api::IoCallBoolResult open_result = open();
      if (!open_result.return_value_) {
        stats_.reopen_failed_.inc();
      } else {
        do_reopen = false;
      }
    }
    // doWrite no matter file isOpen, if not, we can drain buffer
    doWrite(about_to_write_buffer_);
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
  flush_thread_ = thread_factory_.createThread([this]() -> void { flushThreadFunc(); },
                                               Thread::Options{"AccessLogFlush"});
}

} // namespace AccessLog
} // namespace Envoy
