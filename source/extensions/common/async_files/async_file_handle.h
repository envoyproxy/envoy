#pragma once

#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class AsyncFileAction;

class AsyncFileContext : public std::enable_shared_from_this<AsyncFileContext> {
public:
  // Cancels any action that is currently queued. It is important to call abort if there is
  // a request in flight whose on_complete callback's context is being destroyed.
  //
  // If the action is already being performed, abort cannot cancel the action, but will
  // prevent calling of the callback.
  //
  // If the callback is currently being executed in a file worker thread, abort will block
  // until file worker finishes the callback. (Callbacks should be extremely short-lived.)
  // This is so that if the owner of the callback's context is being destroyed, we can
  // guarantee that the context outlives any calls to it (e.g. we don't dispatch to a
  // deleted event queue.)
  virtual void abort() = 0;

  // Action to create and open a temporary file.
  // It is an error to createAnonymousFile on an AsyncFileContext that already has a file open.
  //
  // The path parameter is a path to a directory in which the anonymous file will be
  // created (commonly "/tmp", for example). Even though an anonymous file is not linked and
  // has no filename, the path can be important as it determines which physical hardware the file
  // is written to (i.e. if you were to link() the file later, linking it to a path on a different
  // device is an expensive operation; or you might prefer to write temporary files to a virtual
  // filesystem or to a mounted disposable SSD.)
  //
  // There must not already be an action queued for this handle.
  virtual void createAnonymousFile(std::string path,
                                   std::function<void(absl::Status)> on_complete) = 0;

  // Action to hard link the file that is currently open. Typically for use in tandem with
  // createAnonymousFile to turn that file into a named file after finishing writing its contents.
  virtual void createHardLink(std::string filename,
                              std::function<void(absl::Status)> on_complete) = 0;

  // Action to unlink (delete) a file by name. This has no bearing on the status of any currently
  // open file associated with this handle (except in that it may happen to be referring to the same
  // file).
  virtual void unlink(std::string filename, std::function<void(absl::Status)> on_complete) = 0;

  enum class Mode { READONLY, WRITEONLY, READWRITE };

  // Action to asynchronously open a named file that already exists.
  // on_complete receives OK on success, or an error on failure.
  virtual void openExistingFile(std::string filename, Mode mode,
                                std::function<void(absl::Status)> on_complete) = 0;

  // Enqueues an action to close the currently open file.
  // It is an error to close on an AsyncFileContext that does not have a file open.
  // There must not already be an action queued for this handle.
  virtual void close(std::function<void(absl::Status)> on_complete) = 0;

  // Enqueues an action to read from the currently open file, at position offset, up to the number
  // of bytes specified by length. The size of the buffer passed to on_complete informs you if less
  // than the requested amount was read. It is an error to read on an AsyncFileContext that does not
  // have a file open. There must not already be an action queued for this handle.
  virtual void read(off_t offset, size_t length,
                    std::function<void(absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>>)>
                        on_complete) = 0;

  // Enqueues an action to write to the currently open file, at position offset, the bytes contained
  // by contents. It is an error to call write on an AsyncFileContext that does not have a file
  // open.
  //
  // This call consumes the 'contents' buffer immediately, by move, so it is safe to discard the
  // buffer after the call, and is not safe to assume it still contains the same data.
  //
  // on_complete is called with the number of bytes written on success.
  // There must not already be an action queued for this handle.
  virtual void write(Envoy::Buffer::Instance& contents, off_t offset, // NOLINT(runtime/references)
                     std::function<void(absl::StatusOr<size_t>)> on_complete) = 0;

  // Enqueues a no-op action. This is useful in the case that you have a file action you wish to
  // perform, but if it can't be performed in a timely fashion you might decide to back out of
  // sending it.
  // Using whenReady, you can wait until your request reaches the front of the thread pool queue
  // before issuing it.
  // There must not already be an action queued for this handle.
  virtual void whenReady(std::function<void(absl::Status)> on_complete) = 0;

  // Creates a new AsyncFileHandle referencing the same file.
  // This is not asynchronous like the other file operations, as it is never a time-consuming
  // operation. Note that a file handle duplicated in this way shares positioning and permissions
  // with the original. Since AsyncFileContext functions are all position-explicit, this should not
  // matter.
  //
  // The user should avoid using duplicate while a close, open or create operation is in flight on
  // the source handle, to avoid provoking a race (the underlying file operation will provide an
  // appropriate error in the event that a race goes the wrong way).
  virtual absl::StatusOr<std::shared_ptr<AsyncFileContext>> duplicate() = 0;

protected:
  virtual ~AsyncFileContext() = default;
};

using AsyncFileHandle = std::shared_ptr<AsyncFileContext>;

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
