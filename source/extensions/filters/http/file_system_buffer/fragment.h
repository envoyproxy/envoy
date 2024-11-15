#pragma once
#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/extensions/common/async_files/async_file_handle.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

using Extensions::Common::AsyncFiles::AsyncFileHandle;
using Extensions::Common::AsyncFiles::CancelFunction;

// Internal implementation detail exposed for use in Fragment's variant.
// Represents a buffer fragment that is in memory.
class MemoryFragment {
public:
  explicit MemoryFragment(Buffer::Instance& buffer);
  explicit MemoryFragment(Buffer::Instance& buffer, size_t size);
  std::unique_ptr<Buffer::Instance> extract();

private:
  std::unique_ptr<Buffer::OwnedImpl> buffer_;
};

// Internal implementation detail exposed for use in Fragment's variant.
// Represents a buffer fragment that is being written to storage.
class WritingFragment {};

// Internal implementation detail exposed for use in Fragment's variant.
// Represents a buffer fragment that is being read from storage.
class ReadingFragment {};

// Internal implementation detail exposed for use in Fragment's variant.
// Represents a buffer fragment that is currently in storage.
class StorageFragment {
public:
  explicit StorageFragment(off_t offset) : offset_(offset) {}
  off_t offset() const { return offset_; }

private:
  const off_t offset_;
};

// A Fragment is a piece of the buffer queue used by the filter.
//
// Each fragment may be in memory, on disk, or in an unusable intermediate state
// while controlled by the AsyncFiles library.
//
// The fragments are queued in sequential order, and may not be in the same order
// in the buffer file, as we write "last fragment first" so that the soonest-needed
// fragments remain in memory as long as possible.
//
// Memory fragments are simply `Buffer::Instance`s. Storage fragments contain just
// enough information to reload a memory fragment from the buffer file.
class Fragment {
public:
  explicit Fragment(Buffer::Instance& buffer);
  explicit Fragment(Buffer::Instance& buffer, size_t size);
  ~Fragment();

  // Starts the transition for this fragment from memory to storage.
  //
  // The on_done callback is sent to the dispatcher function after the file write completes.
  //
  // When called from a filter, the dispatcher function must abort without calling the
  // callback if the filter or fragment has been destroyed.
  absl::StatusOr<CancelFunction> toStorage(AsyncFileHandle file, off_t offset,
                                           Event::Dispatcher& dispatcher,
                                           absl::AnyInvocable<void(absl::Status)> on_done);

  // Starts the transition for this fragment from storage to memory.
  //
  // The on_done callback is sent to the dispatcher function after the file read completes.
  //
  // When called from a filter, the dispatcher function must abort without calling the
  // callback if the filter or fragment has been destroyed.
  absl::StatusOr<CancelFunction> fromStorage(AsyncFileHandle file, Event::Dispatcher& dispatcher,
                                             absl::AnyInvocable<void(absl::Status)> on_done);

  // Moves the buffer from a memory instance to the returned value and resets the fragment to
  // size 0.
  //
  // It is an error to call extract() on a Fragment that is not in memory - an exception
  // will be thrown.
  Buffer::InstancePtr extract();

  // Returns true if the fragment is in memory, false if in storage or transition.
  bool isMemory() const;

  // Returns true if the fragment is in storage, false if in memory or transition.
  bool isStorage() const;

  // Returns the size of the fragment in bytes.
  size_t size() const { return size_; }

private:
  size_t size_;
  absl::variant<MemoryFragment, StorageFragment, ReadingFragment, WritingFragment> data_;

  // Disabling copy and move constructors for safety because long-lived fragment pointers
  // are used.
  Fragment(const Fragment&) = delete;
  Fragment(Fragment&&) = delete;
};

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
