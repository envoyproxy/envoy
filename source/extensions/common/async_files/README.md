# AsyncFileManager

An `AsyncFileManager` should be a singleton or similarly long-lived scope. It represents a
thread pool for performing file operations asynchronously.

`AsyncFileManager` can create `AsyncFileHandle`s via `createAnonymousFile` or `openExistingFile`, can stat a file by name with `stat`, and can delete files via `unlink`.

# AsyncFileHandle

An `AsyncFileHandle` represents a context in which asynchronous file operations can be performed. It is associated with at most one file at a time.

Each action on an AsyncFileHandle is effectively an "enqueue" action, in that it places the action in the manager's execution queue, it does not immediately perform the requested action.

## cancellation

Each action function returns a cancellation function which can be called to remove an action from the queue and prevent the callback from being called. If the execution is already in progress, it may be undone (e.g. a file open operation will close the file if it is opening when cancel is called). The cancel function must only be called from the same thread as the
dispatcher that was provided to the original request, to ensure that cancellation and callback
cannot be happening concurrently.

## callbacks

The callbacks passed to `AsyncFileHandle` and `AsyncFileManager` functions are called from
the thread associated with the provided `dispatcher`, if the action was not cancelled first.

The implementation of `AsyncFileManager` ensures that a `cancel` call from the thread associated with the dispatcher, if called prior to the callback's execution, is guaranteed to prevent the callback from being called, there is no race.

## Possible actions

See `async_file_handle.h` for the actions that can currently be queued on an `AsyncFileHandle`.
