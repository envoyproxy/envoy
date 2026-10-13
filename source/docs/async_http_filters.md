## How to write http filters with asynchronous features

To get started on writing a basic http filter, see [http-filter-example](https://github.com/envoyproxy/envoy-filter-example/tree/main/http-filter-example).

This documentation is to explain the less obvious behaviors when a filter performs some asynchronous action, such as performing something in another thread, making a grpc call, etc.

### How to wait for an asynchronous callback

If your extension initiates a long running operation and needs to wait for its completion before it can complete the current step (e.g. `decodeHeaders`, `encodeHeaders`, etc.) the function must return one of the states that indicates work should not continue, e.g. [`Http::FilterHeadersStatus::StopIteration`](https://github.com/envoyproxy/envoy/blob/2d82c10a467cbb933ed5cb9bdb7eaae4ffa160de/envoy/http/filter.h#L45). For more details on this, see [flow_control.md](flow_control.md).

When the callback for your long running operation is completed, if flow control is paused, it should execute either `sendLocalReply` or `continueDecoding`/`continueEncoding` when the filter is ready to resume normal operation.

If the filter is performing changes to the body of the request or response, it will need to call [`addDecodedData`](https://github.com/envoyproxy/envoy/blob/2d82c10a467cbb933ed5cb9bdb7eaae4ffa160de/envoy/http/filter.h#L514)/`addEncodedData` or [`injectDecodedDataToFilterChain`](https://github.com/envoyproxy/envoy/blob/2d82c10a467cbb933ed5cb9bdb7eaae4ffa160de/envoy/http/filter.h#L538)/`injectEncodedDataToFilterChain` as part of that operation - see linked function comments for more details.

### How to make a callback thread-safe

The behavior of asynchronous filter callbacks can be surprising and non-obvious from examples.

The main focus of how this is handled is that your callback should at some point go through the `dispatcher`, to return control to the filter's original thread. Any operations after it's been passed to the dispatcher can use filter member variables etc. without any synchronization primitives, as the work will be being performed on the same thread.

There are three synchronization concerns:
1. accessing filter members from the callback must be thread-safe.
2. potential races, e.g. a header-related callback is called by the other thread before `decodeHeaders` completes.
3. the filter can be destroyed (e.g. if client disconnected) while the callback is in flight.

The recommended way to resolve these issues is to use the dispatcher, which allows the asynchronous thread to transfer work back to the filter's original thread, thereby avoiding synchronization issues.

There are several distinct examples in the codebase of how these issues are addressed, and it's not always clear how it works from example code alone.

#### Example 1: Using AsyncClient

The [ratelimit filter](https://github.com/envoyproxy/envoy/blob/2d82c10a467cbb933ed5cb9bdb7eaae4ffa160de/source/extensions/filters/http/ratelimit/ratelimit.cc) (as one example) solves for this using `AsyncClient`, which it's not obvious at a glance how it works. The `AsyncClient` captures a reference to the thread's dispatcher at its create-time (from a factory context, rather than from the filter's callbacks). When the async grpc completes, `AsyncClient` passes a lambda which will call the callback function (`complete`) to the dispatcher, so when `complete` is called it's on the filter's thread.

Sync issues are addressed by the fact that `complete` is only called on the filter's thread, so there are no member synchronization issues.

Destruction is addressed by the fact that the filter's `onDestroy` function calls `cancel` on the `AsyncClient`; the guarantee of `AsyncClient` is that when `cancel` completes, *either* the callback is already queued, *or* it won't be queued. Another subtle thing is required here for this to work; after `onDestroy`, a filter's final destruction is pushed onto the dispatcher queue. So if the callback became queued during `onDestroy`, the filter object will still exist when the callback is called, as the destructor will be added to the dispatcher queue only *after* the callback was added.

There's one extra trick about `AsyncClient` that the ratelimit filter handles; it's possible for `AsyncClient` to complete the async operation *immediately*, and call the callback on the calling thread, rather than it being enqueued in the dispatcher. The ratelimit filter has a number of awkward checks to ensure that if the callback occurs *during* `decodeHeaders`, then the callback doesn't call `continueDecoding`, and then `decodeHeaders` returns `Continue` rather than `StopIteration`.

#### Example 2: Calling the dispatcher directly

The [cache filter](https://github.com/envoyproxy/envoy/blob/2d82c10a467cbb933ed5cb9bdb7eaae4ffa160de/source/extensions/filters/http/cache/cache_filter.cc) (as one example) resolves the synchronization problems directly, by all callbacks explicitly calling `decoder_callbacks_->dispatcher()`.

This resolves synchronization, because all code except a *very brief* lambda calling the dispatcher is running exclusively on the filter's thread.

The destruction issue has some special handling - since unlike the `AsyncClient` solution there is no inbuilt `cancel` operation to prevent the callbacks from being called after the filter is destroyed, the lambda captures `CacheFilterWeakPtr self = weak_from_this();`, and only performs its internal operation if that weak_ptr can be locked, i.e. the filter has not been deleted. Another way this could be achieved similarly is with e.g. a `shared_ptr<bool> cancelled_` that could be set during `onDestroy`, and captured and checked by each callback lambda.
