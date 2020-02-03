#include "library/common/http/dispatcher.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/lock_guard.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "library/common/buffer/bridge_fragment.h"
#include "library/common/buffer/utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/network/synthetic_address_impl.h"
#include "library/common/thread/lock_guard.h"

namespace Envoy {
namespace Http {

/**
 * IMPORTANT: stream closure semantics in envoy mobile depends on the fact that the HCM fires a
 * stream reset when the remote side of the stream is closed but the local side remains open.
 * In other words the HCM (like the rest of Envoy) dissallows locally half-open streams.
 * If this changes in Envoy, this file will need to change as well.
 * For implementation details @see Dispatcher::DirectStreamCallbacks::closeRemote.
 */

Dispatcher::DirectStreamCallbacks::DirectStreamCallbacks(DirectStream& direct_stream,
                                                         envoy_http_callbacks bridge_callbacks,
                                                         Dispatcher& http_dispatcher)
    : direct_stream_(direct_stream), bridge_callbacks_(bridge_callbacks),
      http_dispatcher_(http_dispatcher) {}

void Dispatcher::DirectStreamCallbacks::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  ENVOY_LOG(debug, "[S{}] response headers for stream (end_stream={}):\n{}",
            direct_stream_.stream_handle_, end_stream, headers);
  // TODO: ***HACK*** currently Envoy sends local replies in cases where an error ought to be
  // surfaced via the error path. There are ways we can clean up Envoy's local reply path to
  // make this possible, but nothing expedient. For the immediate term this is our only real
  // option. See https://github.com/lyft/envoy-mobile/issues/460

  // The presence of EnvoyUpstreamServiceTime implies these headers are not due to a local reply.
  if (headers.get(Headers::get().EnvoyUpstreamServiceTime) != nullptr) {
    // Testing hook.
    http_dispatcher_.synchronizer_.syncPoint("dispatch_encode_headers");

    // @see Dispatcher::DirectStream::dispatch_lock_ for why this lock is necessary.
    Thread::BasicLockable* mutex = end_stream ? nullptr : &direct_stream_.dispatch_lock_;
    Thread::OptionalReleasableLockGuard lock(mutex);
    if (direct_stream_.dispatchable(end_stream)) {
      ENVOY_LOG(debug,
                "[S{}] dispatching to platform response headers for stream (end_stream={}):\n{}",
                direct_stream_.stream_handle_, end_stream, headers);
      bridge_callbacks_.on_headers(Utility::toBridgeHeaders(headers), end_stream,
                                   bridge_callbacks_.context);
      lock.release();
      closeRemote(end_stream);
    }
    return;
  }

  // Deal with a local response based on the HTTP status code received. Envoy Mobile treats
  // successful local responses as actual success. Envoy Mobile surfaces non-200 local responses as
  // errors via callbacks rather than an HTTP response. This is inline with behaviour of other
  // mobile networking libraries.
  uint64_t response_status = Http::Utility::getResponseStatus(headers);
  switch (response_status) {
  case 200: {
    // @see Dispatcher::DirectStream::dispatch_lock_ for why this lock is necessary.
    Thread::BasicLockable* mutex = end_stream ? nullptr : &direct_stream_.dispatch_lock_;
    Thread::OptionalReleasableLockGuard lock(mutex);
    if (direct_stream_.dispatchable(end_stream)) {
      bridge_callbacks_.on_headers(Utility::toBridgeHeaders(headers), end_stream,
                                   bridge_callbacks_.context);
      lock.release();
      closeRemote(end_stream);
    }
    return;
  }
  case 503:
    error_code_ = ENVOY_CONNECTION_FAILURE;
    break;
  default:
    error_code_ = ENVOY_UNDEFINED_ERROR;
  }
  ENVOY_LOG(debug, "[S{}] intercepted local response", direct_stream_.stream_handle_);
  if (end_stream) {
    // The local stream may or may not have completed.
    // If the local is not closed envoy will fire the reset for us.
    // @see Dispatcher::DirectStreamCallbacks::closeRemote.
    // Otherwise fire the reset from here.
    if (direct_stream_.local_closed_) {
      onReset();
    }
  }
}

void Dispatcher::DirectStreamCallbacks::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "[S{}] response data for stream (length={} end_stream={})",
            direct_stream_.stream_handle_, data.length(), end_stream);
  if (!error_code_.has_value()) {
    // @see Dispatcher::DirectStream::dispatch_lock_ for why this lock is necessary.
    Thread::BasicLockable* mutex = end_stream ? nullptr : &direct_stream_.dispatch_lock_;
    Thread::OptionalReleasableLockGuard lock(mutex);
    if (direct_stream_.dispatchable(end_stream)) {
      ENVOY_LOG(debug,
                "[S{}] dispatching to platform response data for stream (length={} end_stream={})",
                direct_stream_.stream_handle_, data.length(), end_stream);
      bridge_callbacks_.on_data(Buffer::Utility::toBridgeData(data), end_stream,
                                bridge_callbacks_.context);
      lock.release();
      closeRemote(end_stream);
    }
  } else {
    ASSERT(end_stream, "local response has to end the stream with a data frame. If Envoy changes "
                       "this expectation, this code needs to be updated.");
    error_message_ = Buffer::Utility::toBridgeData(data);
    // The local stream may or may not have completed.
    // If the local is not closed envoy will fire the reset for us.
    // @see Dispatcher::DirectStreamCallbacks::closeRemote.
    // Otherwise fire the reset from here.
    if (direct_stream_.local_closed_) {
      onReset();
    }
  }
}

void Dispatcher::DirectStreamCallbacks::encodeTrailers(const HeaderMap& trailers) {
  ENVOY_LOG(debug, "[S{}] response trailers for stream:\n{}", direct_stream_.stream_handle_,
            trailers);
  if (direct_stream_.dispatchable(true)) {
    ENVOY_LOG(debug, "[S{}] dispatching to platform response trailers for stream:\n{}",
              direct_stream_.stream_handle_, trailers);
    bridge_callbacks_.on_trailers(Utility::toBridgeHeaders(trailers), bridge_callbacks_.context);
    closeRemote(true);
  }
}

// n.b: all calls to closeRemote are guarded by a call to dispatchable. Hence the on_complete call
// here does not, and should not call dispatchable.
void Dispatcher::DirectStreamCallbacks::closeRemote(bool end_stream) {
  if (end_stream) {
    // Envoy itself does not currently allow half-open streams where the local half is open
    // but the remote half is closed. Therefore, we fire the on_complete callback
    // to the platform layer whenever remote closes.
    ENVOY_LOG(debug, "[S{}] complete stream", direct_stream_.stream_handle_);
    bridge_callbacks_.on_complete(bridge_callbacks_.context);
    // Likewise cleanup happens whenever remote closes even though
    // local might be open. Note that if local is open Envoy will reset the stream. Calling cleanup
    // here is fine because the stream reset will come through synchronously in the same thread as
    // this closeRemote code. Because DirectStream deletion is deferred, the deletion will happen
    // necessarily after the reset occurs. Thus Dispatcher::DirectStreamCallbacks::onReset will
    // **not** have a dangling reference.
    ENVOY_LOG(debug, "[S{}] scheduling cleanup", direct_stream_.stream_handle_);
    http_dispatcher_.cleanup(direct_stream_.stream_handle_);
  }
}

Stream& Dispatcher::DirectStreamCallbacks::getStream() { return direct_stream_; }

void Dispatcher::DirectStreamCallbacks::onReset() {
  ENVOY_LOG(debug, "[S{}] remote reset stream", direct_stream_.stream_handle_);
  envoy_error_code_t code = error_code_.value_or(ENVOY_STREAM_RESET);
  envoy_data message = error_message_.value_or(envoy_nodata);

  // Testing hook.
  http_dispatcher_.synchronizer_.syncPoint("dispatch_on_error");

  // direct_stream_ will not be a dangling reference even in the case that closeRemote cleaned up
  // because in that case this reset is happening synchronously, with the encoding call that called
  // closeRemote, in the Envoy Main thread. Hence DirectStream destruction which is posted on the
  // Envoy Main thread's event loop will strictly happen after this direct_stream_ reference is
  // used. @see Dispatcher::DirectStreamCallbacks::closeRemote() for more details.
  if (direct_stream_.dispatchable(true)) {
    ENVOY_LOG(debug, "[S{}] dispatching to platform remote reset stream",
              direct_stream_.stream_handle_);
    bridge_callbacks_.on_error({code, message}, bridge_callbacks_.context);

    // All the terminal callbacks only cleanup if they are dispatchable.
    // This ensures that cleanup will happen exactly one time.
    http_dispatcher_.cleanup(direct_stream_.stream_handle_);
  }
}

void Dispatcher::DirectStreamCallbacks::onCancel() {
  // This call is guarded at the call-site @see Dispatcher::DirectStream::resetStream().
  // Therefore, it is dispatched here without protection.
  ENVOY_LOG(debug, "[S{}] dispatching to platform cancel stream", direct_stream_.stream_handle_);
  bridge_callbacks_.on_cancel(bridge_callbacks_.context);
}

Dispatcher::DirectStream::DirectStream(envoy_stream_t stream_handle, Dispatcher& http_dispatcher)
    : stream_handle_(stream_handle), parent_(http_dispatcher) {}

Dispatcher::DirectStream::~DirectStream() {
  ENVOY_LOG(debug, "[S{}] destroy stream", stream_handle_);
}

// TODO(junr03): map from StreamResetReason to Envoy Mobile's error types. Right now all resets
// will be ENVOY_STREAM_RESET.
void Dispatcher::DirectStream::resetStream(StreamResetReason reason) {
  // The Http::ConnectionManager does not destroy the stream in doEndStream() when it calls
  // resetStream on the response_encoder_'s Stream. It is up to the response_encoder_ to
  // runResetCallbacks in order to have the Http::ConnectionManager call doDeferredStreamDestroy in
  // ConnectionManagerImpl::ActiveStream::onResetStream.
  runResetCallbacks(reason);
  callbacks_->onReset();
}

void Dispatcher::DirectStream::closeLocal(bool end_stream) {
  // TODO: potentially guard against double local closure.
  local_closed_ = end_stream;

  // No cleanup happens here because cleanup always happens on remote closure or local reset.
  // @see Dispatcher::DirectStreamCallbacks::closeRemote, and @see Dispatcher::resetStream,
  // respectively.
}

bool Dispatcher::DirectStream::dispatchable(bool close) {
  if (close) {
    // Set closed to true and return true if not previously closed.
    return !closed_.exchange(close);
  }
  return !closed_.load();
}

void Dispatcher::ready(Event::Dispatcher& event_dispatcher, ApiListener& api_listener) {
  Thread::LockGuard lock(ready_lock_);

  // Drain the init_queue_ into the event_dispatcher_.
  for (const Event::PostCb& cb : init_queue_) {
    event_dispatcher.post(cb);
  }

  // Ordering somewhat matters here if concurrency guarantees are loosened (e.g. if
  // we rely on atomics instead of locks).
  event_dispatcher_ = &event_dispatcher;
  api_listener_ = &api_listener;
}

void Dispatcher::post(Event::PostCb callback) {
  Thread::LockGuard lock(ready_lock_);

  // If the event_dispatcher_ is set, then post the functor directly to it.
  if (event_dispatcher_ != nullptr) {
    event_dispatcher_->post(callback);
    return;
  }

  // Otherwise, push the functor to the init_queue_ which will be drained once the
  // event_dispatcher_ is ready.
  init_queue_.push_back(callback);
}

Dispatcher::Dispatcher(std::atomic<envoy_network_t>& preferred_network)
    : preferred_network_(preferred_network),
      address_(std::make_shared<Network::Address::SyntheticAddressImpl>()) {}

envoy_status_t Dispatcher::startStream(envoy_stream_t new_stream_handle,
                                       envoy_http_callbacks bridge_callbacks,
                                       envoy_stream_options) {
  post([this, new_stream_handle, bridge_callbacks]() -> void {
    Dispatcher::DirectStreamSharedPtr direct_stream{new DirectStream(new_stream_handle, *this)};
    direct_stream->callbacks_ =
        std::make_unique<DirectStreamCallbacks>(*direct_stream, bridge_callbacks, *this);

    // Only the initial setting of the api_listener_ is guarded.
    direct_stream->stream_decoder_ =
        &TS_UNCHECKED_READ(api_listener_)->newStream(*direct_stream->callbacks_);

    Thread::ReleasableLockGuard lock(streams_lock_);
    streams_.emplace(new_stream_handle, std::move(direct_stream));
    lock.release();
    ENVOY_LOG(debug, "[S{}] start stream", new_stream_handle);
  });

  return ENVOY_SUCCESS;
}

envoy_status_t Dispatcher::sendHeaders(envoy_stream_t stream, envoy_headers headers,
                                       bool end_stream) {
  post([this, stream, headers, end_stream]() -> void {
    Dispatcher::DirectStreamSharedPtr direct_stream = getStream(stream);
    // If direct_stream is not found, it means the stream has already closed or been reset
    // and the appropriate callback has been issued to the caller. There's nothing to do here
    // except silently swallow this.
    // TODO: handle potential race condition with cancellation or failure get a stream in the
    // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
    // from the caller.
    // https://github.com/lyft/envoy-mobile/issues/301
    if (direct_stream) {
      HeaderMapPtr internal_headers = Utility::toInternalHeaders(headers);
      ENVOY_LOG(debug, "[S{}] request headers for stream (end_stream={}):\n{}", stream, end_stream,
                *internal_headers);
      attachPreferredNetwork(*internal_headers);
      direct_stream->stream_decoder_->decodeHeaders(std::move(internal_headers), end_stream);
      direct_stream->closeLocal(end_stream);
    }
  });

  return ENVOY_SUCCESS;
}

envoy_status_t Dispatcher::sendData(envoy_stream_t stream, envoy_data data, bool end_stream) {
  post([this, stream, data, end_stream]() -> void {
    Dispatcher::DirectStreamSharedPtr direct_stream = getStream(stream);
    // If direct_stream is not found, it means the stream has already closed or been reset
    // and the appropriate callback has been issued to the caller. There's nothing to do here
    // except silently swallow this.
    // TODO: handle potential race condition with cancellation or failure get a stream in the
    // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
    // from the caller.
    // https://github.com/lyft/envoy-mobile/issues/301
    if (direct_stream) {
      // The buffer is moved internally, in a synchronous fashion, so we don't need the lifetime
      // of the InstancePtr to outlive this function call.
      Buffer::InstancePtr buf = Buffer::Utility::toInternalData(data);

      ENVOY_LOG(debug, "[S{}] request data for stream (length={} end_stream={})\n", stream,
                data.length, end_stream);
      direct_stream->stream_decoder_->decodeData(*buf, end_stream);
      direct_stream->closeLocal(end_stream);
    }
  });

  return ENVOY_SUCCESS;
}

// TODO: implement.
envoy_status_t Dispatcher::sendMetadata(envoy_stream_t, envoy_headers) { return ENVOY_FAILURE; }

envoy_status_t Dispatcher::sendTrailers(envoy_stream_t stream, envoy_headers trailers) {
  post([this, stream, trailers]() -> void {
    Dispatcher::DirectStreamSharedPtr direct_stream = getStream(stream);
    // If direct_stream is not found, it means the stream has already closed or been reset
    // and the appropriate callback has been issued to the caller. There's nothing to do here
    // except silently swallow this.
    // TODO: handle potential race condition with cancellation or failure get a stream in the
    // first place. Additionally it is possible to get a nullptr due to bogus envoy_stream_t
    // from the caller.
    // https://github.com/lyft/envoy-mobile/issues/301
    if (direct_stream) {
      HeaderMapPtr internal_trailers = Utility::toInternalHeaders(trailers);
      ENVOY_LOG(debug, "[S{}] request trailers for stream:\n{}", stream, *internal_trailers);
      direct_stream->stream_decoder_->decodeTrailers(std::move(internal_trailers));
      direct_stream->closeLocal(true);
    }
  });

  return ENVOY_SUCCESS;
}

envoy_status_t Dispatcher::resetStream(envoy_stream_t stream) {
  // Testing hook.
  synchronizer_.syncPoint("getStream_on_cancel");

  Dispatcher::DirectStreamSharedPtr direct_stream = getStream(stream);
  if (direct_stream) {

    // Testing hook.
    synchronizer_.syncPoint("dispatch_on_cancel");

    // @see Dispatcher::DirectStream::dispatch_lock_ for why this lock is necessary.
    Thread::ReleasableLockGuard lock(direct_stream->dispatch_lock_);
    if (direct_stream->dispatchable(true)) {
      direct_stream->callbacks_->onCancel();
      lock.release();
      // n.b: this is guarded by the call above. If the onCancel is not dispatchable then that means
      // that another terminal callback has already happened. All terminal callbacks clean up stream
      // state, so there is no need to dispatch here.
      post([this, stream]() -> void {
        Dispatcher::DirectStreamSharedPtr direct_stream = getStream(stream);
        if (direct_stream) {
          // This interaction is important. The runResetCallbacks call synchronously causes Envoy to
          // defer delete the HCM's ActiveStream. That means that the lifetime of the DirectStream
          // only needs to be as long as that deferred delete. Therefore, we synchronously call
          // cleanup here which will defer delete the DirectStream, which by definition will be
          // scheduled **after** the HCM's defer delete as they are scheduled on the same dispatcher
          // context.
          //
          // StreamResetReason::RemoteReset is used as the platform code that issues the
          // cancellation is considered the remote.
          direct_stream->runResetCallbacks(StreamResetReason::RemoteReset);
          cleanup(direct_stream->stream_handle_);
        }
      });
    }
    return ENVOY_SUCCESS;
  }
  return ENVOY_FAILURE;
}

Dispatcher::DirectStreamSharedPtr Dispatcher::getStream(envoy_stream_t stream) {
  Thread::LockGuard lock(streams_lock_);

  auto direct_stream_pair_it = streams_.find(stream);
  // Returning will copy the shared_ptr and increase the ref count. Moreover, this is safe because
  // creation of the return value happens before destruction of local variables:
  // https://stackoverflow.com/questions/33150508/in-c-which-happens-first-the-copy-of-a-return-object-or-local-objects-destru
  return (direct_stream_pair_it != streams_.end()) ? direct_stream_pair_it->second : nullptr;
}

void Dispatcher::cleanup(envoy_stream_t stream_handle) {
  ASSERT(TS_UNCHECKED_READ(event_dispatcher_)->isThreadSafe(),
         "stream cleanup must be performed on the event_dispatcher_'s thread.");
  Dispatcher::DirectStreamSharedPtr direct_stream = getStream(stream_handle);
  RELEASE_ASSERT(direct_stream,
                 "cleanup is a private method that is only called with stream ids that exist");

  // The DirectStream should live through synchronous code that already has a reference to it.
  // Hence why it is scheduled for deferred deletion. If this was all that was needed then it
  // would be sufficient to return a shared_ptr in getStream. However, deferred deletion is still
  // required because in Dispatcher::resetStream the DirectStream needs to live for as long and
  // the HCM's ActiveStream lives. Hence its deletion needs to live beyond the synchronous code in
  // Dispatcher::resetStream.
  // TODO: currently upstream Envoy does not have a deferred delete version for shared_ptr. This
  // means that instead of efficiently queuing the deletes for one event in the event loop, all
  // deletes here get queued up as individual posts.
  TS_UNCHECKED_READ(event_dispatcher_)->post([direct_stream]() -> void {
    ENVOY_LOG(debug, "[S{}] deferred deletion of stream", direct_stream->stream_handle_);
  });
  // However, the entry in the map should not exist after cleanup.
  // Hence why it is synchronously erased from the streams map.
  Thread::ReleasableLockGuard lock(streams_lock_);
  size_t erased = streams_.erase(stream_handle);
  lock.release();
  ASSERT(erased == 1, "cleanup should always remove one entry from the streams map");
  ENVOY_LOG(debug, "[S{}] erased stream from streams container", stream_handle);
}

namespace {
const LowerCaseString ClusterHeader{"x-envoy-mobile-cluster"};
const std::string BaseCluster = "base";
const std::string BaseWlanCluster = "base_wlan";
const std::string BaseWwanCluster = "base_wwan";
} // namespace

void Dispatcher::attachPreferredNetwork(HeaderMap& headers) {
  switch (preferred_network_.load()) {
  case ENVOY_NET_WLAN:
    headers.addReference(ClusterHeader, BaseWlanCluster);
    break;
  case ENVOY_NET_WWAN:
    headers.addReference(ClusterHeader, BaseWwanCluster);
    break;
  case ENVOY_NET_GENERIC:
  default:
    headers.addReference(ClusterHeader, BaseCluster);
  }
}

} // namespace Http
} // namespace Envoy
