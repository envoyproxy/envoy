#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"

#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_handle.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

// An AsyncFileManager should be a singleton or singleton-like.
// Possible subclasses currently are:
//   * AsyncFileManagerThreadPool
class AsyncFileManager {
public:
  virtual ~AsyncFileManager() = default;

  // Action to create and open a temporary file.
  //
  // The path parameter is a path to a directory in which the anonymous file will be
  // created (commonly "/tmp", for example). Even though an anonymous file is not linked and
  // has no filename, the path can be important as it determines which physical hardware the file
  // is written to (i.e. if you were to link() the file later, linking it to a path on a different
  // device is an expensive operation; or you might prefer to write temporary files to a virtual
  // filesystem or to a mounted disposable SSD.)
  // on_complete receives an AsyncFileHandle on success, or an error on failure.
  //
  // Returns a cancellation function, which aborts the operation (and closes
  // the file if opened) unless the callback has already been called.
  virtual CancelFunction
  createAnonymousFile(Event::Dispatcher* dispatcher, absl::string_view path,
                      absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete) PURE;

  // A mode for opening existing files.
  enum class Mode { ReadOnly, WriteOnly, ReadWrite };

  // Action to asynchronously open a named file that already exists.
  // on_complete receives an AsyncFileHandle on success, or an error on failure.
  //
  // Returns a cancellation function, which aborts the operation (and closes
  // the file if opened) unless the callback has already been called.
  virtual CancelFunction
  openExistingFile(Event::Dispatcher* dispatcher, absl::string_view filename, Mode mode,
                   absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete) PURE;

  // Action to stat a file.
  // on_complete receives a stat structure on success, or an error on failure.
  //
  // Returns a cancellation function, which aborts the operation
  // unless the callback has already been called.
  virtual CancelFunction
  stat(Event::Dispatcher* dispatcher, absl::string_view filename,
       absl::AnyInvocable<void(absl::StatusOr<struct stat>)> on_complete) PURE;

  // Action to delete a named file.
  // on_complete receives OK on success, or an error on failure.
  //
  // Returns a cancellation function, which aborts the operation
  // unless it has already been performed.
  virtual CancelFunction unlink(Event::Dispatcher* dispatcher, absl::string_view filename,
                                absl::AnyInvocable<void(absl::Status)> on_complete) PURE;

  // Return a string description of the configuration of the manager.
  // (This is mostly to facilitate testing.)
  virtual std::string describe() const PURE;

  // To facilitate testing, blocks until all queued actions have been performed and
  // callbacks posted.
  // This does not guarantee that the callbacks have been executed, only that they
  // have been sent to the dispatcher.
  virtual void waitForIdle() PURE;

protected:
  class QueuedAction {
  public:
    QueuedAction(std::unique_ptr<AsyncFileAction> action, Event::Dispatcher* dispatcher)
        : action_(std::move(action)), dispatcher_(dispatcher),
          state_(std::make_shared<std::atomic<State>>(State::Queued)) {}
    QueuedAction() = default;
    std::unique_ptr<AsyncFileAction> action_;
    Event::Dispatcher* dispatcher_ = nullptr;
    enum class State { Queued, Executing, InCallback, Done, Cancelled };
    std::shared_ptr<std::atomic<State>> state_;
  };

private:
  // Puts an action in the queue for execution.
  virtual CancelFunction enqueue(Event::Dispatcher* dispatcher,
                                 std::unique_ptr<AsyncFileAction> action) PURE;
  // Puts an action in the queue for its `onCancelledBeforeCallback` function to be
  // called.
  virtual void postCancelledActionForCleanup(std::unique_ptr<AsyncFileAction> action) PURE;

  friend class AsyncFileContextBase;
  friend class AsyncFileManagerTest;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
