#pragma once

#include <memory>

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
  createAnonymousFile(absl::string_view path,
                      std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) PURE;

  // A mode for opening existing files.
  enum class Mode { ReadOnly, WriteOnly, ReadWrite };

  // Action to asynchronously open a named file that already exists.
  // on_complete receives an AsyncFileHandle on success, or an error on failure.
  //
  // Returns a cancellation function, which aborts the operation (and closes
  // the file if opened) unless the callback has already been called.
  virtual CancelFunction
  openExistingFile(absl::string_view filename, Mode mode,
                   std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) PURE;

  // Action to stat a file.
  // on_complete receives a stat structure on success, or an error on failure.
  //
  // Returns a cancellation function, which aborts the operation
  // unless the callback has already been called.
  virtual CancelFunction stat(absl::string_view filename,
                              std::function<void(absl::StatusOr<struct stat>)> on_complete) PURE;

  // Action to delete a named file.
  // on_complete receives OK on success, or an error on failure.
  //
  // Returns a cancellation function, which aborts the operation
  // unless it has already been performed.
  virtual CancelFunction unlink(absl::string_view filename,
                                std::function<void(absl::Status)> on_complete) PURE;

  // whenReady can be used to only perform an action when the caller hits the
  // front of the thread pool's queue - this can be used to defer requesting
  // a file action until it could actually take place. For example, if you're
  // offloading data from memory to disk temporarily, if you queue the write
  // immediately then the filesystem thread owns the data until the write
  // completes, which may be blocked by heavy traffic, and it turns out you
  // want the data back before then - you can't get it back, you have to wait
  // for the write to complete and then read it back.
  //
  // If you used whenReady, you could keep the data belonging to the client
  // until it's actually the client's turn to do disk access. When whenReady's
  // callback is called, if you request the write at that time the performance
  // will be almost identical to if you had requested the write earlier, but
  // you have the opportunity to change your mind and do something different
  // in the meantime.
  //
  // The cost of using whenReady is that it requires the client to be lock
  // controlled (since the callback occurs in a different thread than the thread
  // the state belongs to), versus simpler unchained operations can use queue
  // based actions and not worry about ownership.
  CancelFunction whenReady(std::function<void(absl::Status)> on_complete);

  // Return a string description of the configuration of the manager.
  // (This is mostly to facilitate testing.)
  virtual std::string describe() const PURE;

private:
  virtual CancelFunction enqueue(const std::shared_ptr<AsyncFileAction> context) PURE;

  friend class AsyncFileContextBase;
  friend class AsyncFileManagerTest;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
