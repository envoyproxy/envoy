# AsyncFileManager

An `AsyncFileManager` should be a singleton or similarly long-lived scope. It represents a
thread pool for performing file operations asynchronously.

## newFileHandle()

Returns an `AsyncFileHandle` that will use the `AsyncFileManager`'s thread pool.

# AsyncFileHandle

An `AsyncFileHandle` represents a context in which asynchronous file operations can be performed. It is associated with at most one file at a time.
Each action on an AsyncFileHandle is effectively an "enqueue" action, in that it places the action in the thread pool's execution queue, it does not immediately perform the requested action. Actions on an `AsyncFileHandle` can be *chained*, by enqueuing another action during the callback from a previous action, e.g.

```
handle->createAnonymousFile("/tmp", [handle](absl::Status opened) {
  if (!opened.ok()) {
    std::cout << "oh no, an error: " << opened << std::endl;
    return;
  }
  handle->write(someBuffer, 0, [handle](absl::StatusOr<size_t> written) {
    if (!written.ok()) {
      std::cout << "oh no, an error: " << written << std::endl;
      return;
    }
    std::cout << "wrote " << *written << " bytes" << std::endl;
    handle->close([](absl::Status closed) {
      if (!closed.ok()) {
        std::cout << "oh no, an error: " << closed << std::endl;
      }
    });
  });
});
```

Will open an unnamed file, write 5 bytes, and close it. (This is just for explanatory purposes, in practice you would most likely want the callbacks to call something on `this` rather than nesting lambdas!)

Chaining actions, as opposed to enqueuing, waiting for some other event, and enqueuing again, will not yield the thread in the thread-pool. An advantage of this is that, for example, if 5 workers all wanted to write a 100kb file at the same moment, with unchained requests in a one-thread threadpool the sequence would most likely resemble

```
OPEN-OPEN-OPEN-OPEN-OPEN-WRITE-WRITE-WRITE-WRITE-WRITE-CLOSE-CLOSE-CLOSE-CLOSE-CLOSE
```

Versus with appropriately chained requests in a one-thread threadpool the sequence would be guaranteed to be

```
OPEN-WRITE-CLOSE-OPEN-WRITE-CLOSE-OPEN-WRITE-CLOSE-OPEN-WRITE-CLOSE-OPEN-WRITE-CLOSE
```

Expand this concept to 100+ files all asking to be written at once and you can immediately see the advantages of chaining; not having the resource issues of many files open at the same time, more localized access, etc.

## abort()

`abort` cancels the `AsyncFileHandle`'s current action, and queues closure of any file that is currently open. `abort` should be called if a file is open or if an already-queued action's callback's captured context is being invalidated (i.e. it is generally a good idea to put an `abort` in the destructor of any object that contains an `AsyncFileHandle`).

If an action is already executing, `abort` returns immediately, the action finishes asynchronously, and the callback is not called.

If a callback is already being called, `abort` blocks until the callback returns. (See callbacks below, this should never be long-blocking!)

## callbacks

The callbacks passed to `AsyncFileHandle` are scheduled in the shared thread pool - therefore they should be doing minimal work, not blocking, and return promptly. If any significant work or blocking is required, the result of the previous action should be passed from the callback to another thread (via some dispatcher or other queuing mechanism) so the async file thread pool can continue performing file operations.

NOTE: Callbacks *must not* reference variables on the stack of the thread that enqueued the action, as such references will be invalid in the context of the thread that runs the callback, even if the stack context hasn't exited by the time the callback is called.

## Possible actions

See `async_file_handle.h` for the actions that can currently be queued.

