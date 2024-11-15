#pragma once

#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/async_files/async_file_action.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

// The context for AsyncFile operations that operate on an open file.
// Instantiated from an AsyncFileManager.
class AsyncFileContext : public std::enable_shared_from_this<AsyncFileContext> {
public:
  // Gets a stat struct for the file.
  virtual absl::StatusOr<CancelFunction>
  stat(Event::Dispatcher* dispatcher,
       absl::AnyInvocable<void(absl::StatusOr<struct stat>)> on_complete) PURE;

  // Action to hard link the file that is currently open. Typically for use in tandem with
  // createAnonymousFile to turn that file into a named file after finishing writing its contents.
  //
  // If cancelled before the callback is called but after creating the file, unlinks the file.
  virtual absl::StatusOr<CancelFunction>
  createHardLink(Event::Dispatcher* dispatcher, absl::string_view filename,
                 absl::AnyInvocable<void(absl::Status)> on_complete) PURE;

  // Enqueues an action to close the currently open file.
  // It is an error to use an AsyncFileContext after calling close.
  // It is an error to destroy an AsyncFileHandle without calling close.
  // It is an error to call close twice.
  // Note that because an AsyncFileHandle is a shared_ptr, it is okay to
  // call close during the handle's owner's destructor - that will queue the close
  // event, which will keep the handle alive until after the close operation
  // is completed.
  // Cancelling close can abort the callback, but the close action will always complete.
  virtual absl::StatusOr<CancelFunction>
  close(Event::Dispatcher* dispatcher, absl::AnyInvocable<void(absl::Status)> on_complete) PURE;

  // Enqueues an action to read from the currently open file, at position offset, up to the number
  // of bytes specified by length. The size of the buffer passed to on_complete informs you if less
  // than the requested amount was read. It is an error to read on an AsyncFileContext that does not
  // have a file open. There must not already be an action queued for this handle.
  virtual absl::StatusOr<CancelFunction>
  read(Event::Dispatcher* dispatcher, off_t offset, size_t length,
       absl::AnyInvocable<void(absl::StatusOr<Buffer::InstancePtr>)> on_complete) PURE;

  // Enqueues an action to write to the currently open file, at position offset, the bytes contained
  // by contents. It is an error to call write on an AsyncFileContext that does not have a file
  // open.
  //
  // This call consumes the 'contents' buffer immediately, by move, so it is safe to discard the
  // buffer after the call, and is not safe to assume it still contains the same data.
  //
  // on_complete is called with the number of bytes written on success.
  // There must not already be an action queued for this handle.
  virtual absl::StatusOr<CancelFunction>
  write(Event::Dispatcher* dispatcher, Buffer::Instance& contents, off_t offset,
        absl::AnyInvocable<void(absl::StatusOr<size_t>)> on_complete) PURE;

  // Creates a new AsyncFileHandle referencing the same file.
  // Note that a file handle duplicated in this way shares positioning and permissions
  // with the original. Since AsyncFileContext functions are all position-explicit, this should not
  // matter.
  virtual absl::StatusOr<CancelFunction> duplicate(
      Event::Dispatcher* dispatcher,
      absl::AnyInvocable<void(absl::StatusOr<std::shared_ptr<AsyncFileContext>>)> on_complete) PURE;

protected:
  virtual ~AsyncFileContext() = default;
};

using AsyncFileHandle = std::shared_ptr<AsyncFileContext>;

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
