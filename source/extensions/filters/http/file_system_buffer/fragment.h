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

class FragmentData;
using UpdateFragmentFunction = std::function<void()>;
using Extensions::Common::AsyncFiles::AsyncFileHandle;
using Extensions::Common::AsyncFiles::CancelFunction;

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
  // The on_done callback is called when the file write completes.
  //
  // The callback includes its own callback parameter update_fragment (on success),
  // so that the operation of updating the fragment can be dispatched to be performed
  // in the envoy worker thread, rather than being performed on the callback thread.
  // (This is important to allow for proper handling if the context was deleted while
  // the operation was in flight, for example.)
  // i.e. typical usage resembles:
  // auto result = fragment.toStorage(file_handle, offset,
  //     [this](absl::StatusOr<UpdateFragmentFunction> status_or_update_fragment) {
  //   if (status_or_update_fragment.ok()) {
  //     auto update_fragment = status_or_update_fragment.value();
  //     dispatcher->dispatch([this, update_fragment]() {
  //       update_fragment();
  //       // do more stuff with the fragment, or in reaction to it being updated.
  //     });
  //   } else {
  //     dispatcher->dispatch([this, status = status_or_update_fragment.status()]() {
  //       // do stuff in response to the error.
  //     });
  //   }
  // });
  absl::StatusOr<CancelFunction>
  toStorage(AsyncFileHandle file, off_t offset,
            std::function<void(absl::StatusOr<UpdateFragmentFunction>)> on_done);

  // Starts the transition for this fragment from storage to memory.
  //
  // The on_done callback is called when the file read completes.
  //
  // The callback includes its own callback parameter update_fragment (on success),
  // so that the operation of updating the fragment can be dispatched to be performed
  // in the envoy worker thread, rather than being performed on the callback thread.
  // (This is important to allow for proper handling if the context was deleted while
  // the operation was in flight, for example.)
  //
  // See toStorage for example usage.
  absl::StatusOr<CancelFunction>
  fromStorage(AsyncFileHandle file,
              std::function<void(absl::StatusOr<UpdateFragmentFunction>)> on_done);

  // Removes the buffer from a memory instance and resets it to size 0.
  //
  // It is an error to call extract() on a Fragment that is not in memory.
  Buffer::InstancePtr extract();

  // Returns true if the fragment is in memory, false if in storage or transition.
  bool isMemory() const;

  // Returns true if the fragment is in storage, false if in memory or transition.
  bool isStorage() const;

  // Returns the size of the fragment in bytes.
  size_t size() const { return size_; }

private:
  size_t size_;
  std::unique_ptr<FragmentData> data_;
};

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
