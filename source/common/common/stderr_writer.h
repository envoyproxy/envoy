#pragma once

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "absl/strings/string_view.h"

namespace Envoy {

/**
 * Builds a line of output in a fixed buffer and writes it to stderr with a single write(2) when
 * it goes out of scope.
 *
 * This is for use where neither the logger nor C++ streams can be relied on, in particular in a
 * fatal signal handler: it does not allocate, and it takes none of the logger, stdio or stream
 * locks, any of which the interrupted thread may already hold.
 *
 * Output that does not fit in the buffer is truncated, keeping a trailing newline.
 *
 *   StderrWriter() << "Caught " << name << ", address " << address << "\n";
 */
class StderrWriter {
public:
  StderrWriter() = default;
  StderrWriter(const StderrWriter&) = delete;
  StderrWriter& operator=(const StderrWriter&) = delete;
  ~StderrWriter() { flush(); }

  StderrWriter& operator<<(absl::string_view text) {
    const size_t size = std::min(text.size(), Capacity - size_);
    std::copy_n(text.data(), size, buffer_ + size_);
    size_ += size;
    truncated_ = truncated_ || size < text.size();
    return *this;
  }

  StderrWriter& operator<<(const char* text) {
    return *this << (text != nullptr ? absl::string_view(text) : absl::string_view("(null)"));
  }

  // Formatted as a decimal.
  StderrWriter& operator<<(int value) {
    char digits[16];
    size_t size = 0;
    // Negate as unsigned so that the most negative value does not overflow.
    unsigned int remaining =
        value < 0 ? 0U - static_cast<unsigned int>(value) : static_cast<unsigned int>(value);
    do {
      digits[size++] = static_cast<char>('0' + remaining % 10);
      remaining /= 10;
    } while (remaining != 0);
    if (value < 0) {
      digits[size++] = '-';
    }
    std::reverse(digits, digits + size);
    return *this << absl::string_view(digits, size);
  }

  // Formatted as `0x` and lower case hex without leading zeros, whatever the platform. This is
  // what `fmt::ptr` produces, and what `std::ostream` produces other than with the MSVC library.
  StderrWriter& operator<<(const void* address) {
    char digits[2 * sizeof(uintptr_t)];
    size_t size = 0;
    uintptr_t remaining = reinterpret_cast<uintptr_t>(address);
    do {
      digits[size++] = "0123456789abcdef"[remaining % 16];
      remaining /= 16;
    } while (remaining != 0);
    std::reverse(digits, digits + size);
    return *this << absl::string_view("0x") << absl::string_view(digits, size);
  }

  /**
   * Write out what has been built so far. This also happens on destruction.
   */
  void flush() { flush(&StderrWriter::writeStderr); }

  // Large enough for a symbol name (see `BackwardsTrace`) or a path, with its decoration.
  static constexpr size_t Capacity = 4608;

private:
  friend class StderrWriterPeer;

  // As write(2) on stderr: the number of bytes written, or -1 with `errno` set.
  using WriteFn = int64_t (*)(const char* data, size_t size);

  static int64_t writeStderr(const char* data, size_t size) {
#ifdef WIN32
    return ::_write(2, data, static_cast<unsigned int>(size));
#else
    return ::write(STDERR_FILENO, data, size);
#endif
  }

  // Short writes and interrupted writes are continued. Any other failure drops what is left, as
  // there is nowhere to report it.
  void flush(WriteFn write_fn) {
    if (truncated_ && size_ > 0) {
      buffer_[size_ - 1] = '\n';
    }
    size_t written = 0;
    while (written < size_) {
      const int64_t rc = write_fn(buffer_ + written, size_ - written);
      if (rc < 0) {
        if (errno == EINTR) {
          continue;
        }
        break;
      }
      written += static_cast<size_t>(rc);
    }
    size_ = 0;
    truncated_ = false;
  }

  char buffer_[Capacity];
  size_t size_{0};
  bool truncated_{false};
};

} // namespace Envoy
