#include "filesystem_impl.h"

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/thread.h"

#include <dirent.h>
#include <fcntl.h>
#include <iostream>

namespace Filesystem {
bool fileExists(const std::string& path) {
  std::ifstream input_file(path);
  return input_file.is_open();
}

bool directoryExists(const std::string& path) {
  DIR* dir = opendir(path.c_str());
  bool dirExists = nullptr != dir;
  closedir(dir);

  return dirExists;
}

std::string fileReadToEnd(const std::string& path) {
  std::ios::sync_with_stdio(false);

  std::ifstream file(path);
  if (!file) {
    throw EnvoyException(fmt::format("unable to read file: {}", path));
  }

  std::stringstream file_string;
  file_string << file.rdbuf();

  return file_string.str();
}

int OsSysCallsImpl::open(const std::string& full_path, int flags, int mode) {
  return ::open(full_path.c_str(), flags, mode);
}

int OsSysCallsImpl::close(int fd) { return ::close(fd); }

ssize_t OsSysCallsImpl::write(int fd, const void* buffer, size_t num_bytes) {
  return ::write(fd, buffer, num_bytes);
}

FileImpl::FileImpl(const std::string& path, Event::Dispatcher& dispatcher,
                   Thread::BasicLockable& lock, OsSysCalls& os_sys_calls, Stats::Store& stats_store,
                   std::chrono::milliseconds flush_interval_msec)
    : path_(path), lock_(lock), dispatcher_(dispatcher), os_sys_calls_(os_sys_calls),
      flush_interval_msec_(flush_interval_msec),
      stats_{FILESYSTEM_STATS(POOL_COUNTER_PREFIX(stats_store, "filesystem."),
                              POOL_GAUGE_PREFIX(stats_store, "filesystem."))} {
  open();
}

void FileImpl::open() {
  fd_ = os_sys_calls_.open(path_.c_str(), O_RDWR | O_APPEND | O_CREAT,
                           S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  if (-1 == fd_) {
    throw EnvoyException(fmt::format("unable to open file '{}': {}", path_, strerror(errno)));
  }
}

void FileImpl::reopen() { reopen_file_ = true; }

FileImpl::~FileImpl() {
  {
    std::unique_lock<Thread::BasicLockable> lock(lock_);
    flush_thread_exit_ = true;
    flush_event_.notify_one();
  }

  if (flush_thread_ != nullptr) {
    flush_thread_->join();
  }

  // Flush any remaining data. If file was not opened for some reason, skip flushing part.
  if (fd_ != -1) {
    if (flush_buffer_.length() > 0) {
      doWrite(flush_buffer_);
      stats_.write_total_buffered_.sub(flush_buffer_.length());
    }

    os_sys_calls_.close(fd_);
  }
}

void FileImpl::doWrite(Buffer::Instance& buffer) {
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  buffer.getRawSlices(slices, num_slices);
  for (Buffer::RawSlice& slice : slices) {
    ssize_t rc = os_sys_calls_.write(fd_, slice.mem_, slice.len_);
    ASSERT(rc == static_cast<ssize_t>(slice.len_));
    UNREFERENCED_PARAMETER(rc);
    stats_.write_completed_.inc();
  }
}

void FileImpl::flushThreadFunc() {
  std::unique_lock<Thread::BasicLockable> lock(lock_);

  while (true) {
    // flush_event_ can be woken up either by large enough flush_buffer or by timer.
    // In case it was timer, flush_buffer_ can be empty.
    while (flush_buffer_.length() == 0 && !flush_thread_exit_) {
      flush_event_.wait(lock);
    }

    if (flush_thread_exit_) {
      return;
    }

    ASSERT(flush_buffer_.length() > 0);
    Buffer::RawSlice slices[1];
    flush_buffer_.getRawSlices(slices, 1);
    Buffer::OwnedImpl copy(slices[0].mem_, slices[0].len_);
    flush_buffer_.drain(slices[0].len_);
    stats_.write_total_buffered_.sub(slices[0].len_);

    lock.unlock();

    // if we failed to open file before (-1 == fd_), then simply ignore
    if (fd_ != -1) {
      try {
        if (reopen_file_) {
          reopen_file_ = false;
          os_sys_calls_.close(fd_);
          open();
        }

        doWrite(copy);
      } catch (const EnvoyException&) {
        stats_.reopen_failed_.inc();
      }
    }

    lock.lock();
  }
}

void FileImpl::write(const std::string& data) {
  std::unique_lock<Thread::BasicLockable> lock(lock_);

  if (flush_thread_ == nullptr) {
    createFlushStructures();
  }

  stats_.write_buffered_.inc();
  stats_.write_total_buffered_.add(data.length());
  flush_buffer_.add(data);
  if (flush_buffer_.length() > MIN_FLUSH_SIZE) {
    flush_event_.notify_one();
  }
}

void FileImpl::createFlushStructures() {
  flush_thread_.reset(new Thread::Thread([this]() -> void { flushThreadFunc(); }));
  flush_timer_ = dispatcher_.createTimer([this]() -> void {
    stats_.flushed_by_timer_.inc();
    flush_event_.notify_one();
    flush_timer_->enableTimer(flush_interval_msec_);
  });
  flush_timer_->enableTimer(flush_interval_msec_);
}

} // Filesystem
