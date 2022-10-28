# AsyncFileManager

An `AsyncFileManager` should be a singleton or similarly long-lived scope. It represents a
thread pool for performing file operations asynchronously.

`AsyncFileManager` can create `AsyncFileHandle`s via `createAnonymousFile` or `openExistingFile`,
can postpone queuing file actions using `whenReady`, and can delete files via `unlink`.

# AsyncFileHandle

An `AsyncFileHandle` represents a context in which asynchronous file operations can be performed. It is associated with at most one file at a time.

Each action on an AsyncFileHandle is effectively an "enqueue" action, in that it places the action in the manager's execution queue, it does not immediately perform the requested action. Actions on an `AsyncFileHandle` can be *chained*, by enqueuing another action during the callback from a previous action, e.g.

```
manager->createAnonymousFile("/tmp", [](absl::StatusOr<AsyncFileHandle> opened) {
  if (!opened.ok()) {
    std::cout << "oh no, an error: " << opened.status() << std::endl;
    return;
  }
  auto handle = opened.value();
  handle->write(someBuffer, 0, [handle](absl::StatusOr<size_t> written) {
    if (!written.ok()) {
      std::cout << "oh no, an error: " << written.status() << std::endl;
      return;
    }
    std::cout << "wrote " << written.value() << " bytes" << std::endl;
    handle->close([](absl::Status closed) {
      if (!closed.ok()) {
        std::cout << "oh no, an error: " << closed << std::endl;
      }
    }).IgnoreError(); // A returned error only occurs if the file handle was closed.
  }).IgnoreError(); // A returned error only occurs if the file handle was closed.
});
```

Will open an unnamed file, write 5 bytes, and close it. (This is just for explanatory purposes, in practice you would most likely want the callbacks to call something on `this` rather than nesting lambdas!)

Chaining actions, as opposed to enqueuing, passing the result to a main thread, and from there enqueuing again, will not yield the thread in a thread-pool based implementation. An advantage of this is that, for example, if 5 workers all wanted to write a 100kb file at the same moment, with unchained requests in a one-thread threadpool the sequence would most likely resemble

```
OPEN-OPEN-OPEN-OPEN-OPEN-WRITE-WRITE-WRITE-WRITE-WRITE-CLOSE-CLOSE-CLOSE-CLOSE-CLOSE
```

Versus with appropriately chained requests in a one-thread threadpool the sequence would be guaranteed to be

```
OPEN-WRITE-CLOSE-OPEN-WRITE-CLOSE-OPEN-WRITE-CLOSE-OPEN-WRITE-CLOSE-OPEN-WRITE-CLOSE
```

Expand this concept to 100+ files all asking to be written at once and you can immediately see the advantages of chaining; not having the resource issues of many files open at the same time, more localized access, etc.

## cancellation

Each action function returns a cancellation function which can be called to remove an action from the queue and prevent the callback from being called. If the execution is already in progress, it may be undone (e.g. a file open operation will close the file if it is opening when cancel is called). The cancel function will block if the callback is already in progress when cancel is called, until the callback completes. This should not be a long block, as callbacks should be short (see callbacks below).

As such, a client should ensure that the cleanup order is consistent - if a callback captures a file handle, the client should clean up that file handle (if present) *after* calling cancel, in case the file was opened during the call to cancel.

## callbacks

The callbacks passed to `AsyncFileHandle` and `AsyncFileManager` are scheduled in a thread or thread pool belonging to the AsyncFileManager - therefore they should be doing minimal work, not blocking (for more than a trivial data-guard lock), and return promptly. If any significant work or blocking is required, the result of the previous action should be passed from the callback to another thread (via some dispatcher or other queuing mechanism) so the manager's thread can continue performing file operations for other clients.

## Possible actions

See `async_file_handle.h` for the actions that can currently be queued on an `AsyncFileHandle`.
