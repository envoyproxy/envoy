#pragma once

#include <unordered_map>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/async_client.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/common/thread.h"

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Http {

/**
 * Manages HTTP streams, and provides an interface to interact with them.
 * The Dispatcher executes all stream operations on the provided Event::Dispatcher's event loop.
 */
class Dispatcher : public Logger::Loggable<Logger::Id::http> {
public:
  void ready(Event::Dispatcher& event_dispatcher, Upstream::ClusterManager& cluster_manager);

  /**
   * Attempts to open a new stream to the remote. Note that this function is asynchronous and
   * opening a stream may fail. The returned handle is immediately valid for use with this API, but
   * there is no guarantee it will ever functionally represent an open stream.
   * @param bridge_callbacks wrapper for callbacks for events on this stream.
   * @return envoy_stream_t handle to the stream being created.
   */
  envoy_status_t startStream(envoy_stream_t stream, envoy_http_callbacks bridge_callbacks);

  /**
   * Send headers over an open HTTP stream. This method can be invoked once and needs to be called
   * before send_data.
   * @param stream, the stream to send headers over.
   * @param headers, the headers to send.
   * @param end_stream, indicates whether to close the stream locally after sending this frame.
   * @return envoy_status_t, the resulting status of the operation.
   */
  envoy_status_t sendHeaders(envoy_stream_t stream, envoy_headers headers, bool end_stream);

  /**
   * Send data over an open HTTP stream. This method can be invoked multiple times.
   * @param stream, the stream to send data over.
   * @param data, the data to send.
   * @param end_stream, indicates whether to close the stream locally after sending this frame.
   * @return envoy_status_t, the resulting status of the operation.
   */
  envoy_status_t sendData(envoy_stream_t stream, envoy_data data, bool end_stream);

  /**
   * Send metadata over an HTTP stream. This method can be invoked multiple times.
   * @param stream, the stream to send metadata over.
   * @param metadata, the metadata to send.
   * @return envoy_status_t, the resulting status of the operation.
   */
  envoy_status_t sendMetadata(envoy_stream_t stream, envoy_headers metadata);

  /**
   * Send trailers over an open HTTP stream. This method can only be invoked once per stream.
   * Note that this method implicitly closes the stream locally.
   * @param stream, the stream to send trailers over.
   * @param trailers, the trailers to send.
   * @return envoy_status_t, the resulting status of the operation.
   */
  envoy_status_t sendTrailers(envoy_stream_t stream, envoy_headers trailers);

  /**
   * Reset an open HTTP stream. This operation closes the stream locally, and remote.
   * No further operations are valid on the stream.
   * @param stream, the stream to reset.
   * @return envoy_status_t, the resulting status of the operation.
   */
  envoy_status_t resetStream(envoy_stream_t stream);

private:
  /**
   * Notifies caller of async HTTP stream status.
   * Note the HTTP stream is full-duplex, even if the local to remote stream has been ended
   * by sendHeaders/sendData with end_stream=true, sendTrailers, or locallyCloseStream
   * DirectStreamCallbacks can continue to receive events until the remote to local stream is
   * closed, or resetStream is called.
   */
  class DirectStreamCallbacks : public AsyncClient::StreamCallbacks,
                                public Logger::Loggable<Logger::Id::http> {
  public:
    DirectStreamCallbacks(envoy_stream_t stream_handle, envoy_http_callbacks bridge_callbacks,
                          Dispatcher& http_dispatcher);

    // AsyncClient::StreamCallbacks
    void onHeaders(HeaderMapPtr&& headers, bool end_stream) override;
    void onData(Buffer::Instance& data, bool end_stream) override;
    void onTrailers(HeaderMapPtr&& trailers) override;
    void onComplete() override;
    void onReset() override;

  private:
    const envoy_stream_t stream_handle_;
    const envoy_http_callbacks bridge_callbacks_;
    Dispatcher& http_dispatcher_;
  };

  using DirectStreamCallbacksPtr = std::unique_ptr<DirectStreamCallbacks>;

  /**
   * Contains state about an HTTP stream; both in the outgoing direction via an underlying
   * AsyncClient::Stream and in the incoming direction via DirectStreamCallbacks.
   */
  class DirectStream {
  public:
    DirectStream(envoy_stream_t stream_handle, AsyncClient::Stream& underlying_stream,
                 DirectStreamCallbacksPtr&& callbacks);

    const envoy_stream_t stream_handle_;
    // Used to issue outgoing HTTP stream operations.
    AsyncClient::Stream& underlying_stream_;
    // Used to receive incoming HTTP stream operations.
    const DirectStreamCallbacksPtr callbacks_;

    HeaderMapPtr headers_;
    // TODO: because the client may send infinite metadata frames we need some ongoing way to
    // free metadata ahead of object destruction.
    // An implementation option would be to have drainable header maps, or done callbacks.
    std::vector<HeaderMapPtr> metadata_;
    HeaderMapPtr trailers_;
  };

  using DirectStreamPtr = std::unique_ptr<DirectStream>;

  /**
   * Post a functor to the dispatcher. This is safe cross thread.
   * @param callback, the functor to post.
   */
  void post(Event::PostCb callback);
  // Everything in the below interface must only be accessed from the event_dispatcher's thread.
  // This allows us to generally avoid synchronization.
  DirectStream* getStream(envoy_stream_t stream_handle);
  void cleanup(envoy_stream_t stream_handle);

  // The dispatch_lock_ and init_queue_, and event_dispatcher_ are the only member state that may
  // be accessed from a thread other than the event_dispatcher's own thread.
  Thread::MutexBasicLockable dispatch_lock_;
  std::list<Event::PostCb> init_queue_ GUARDED_BY(dispatch_lock_);
  Event::Dispatcher* event_dispatcher_ GUARDED_BY(dispatch_lock_);
  Upstream::ClusterManager* cluster_manager_ GUARDED_BY(dispatch_lock_);
  std::unordered_map<envoy_stream_t, DirectStreamPtr> streams_;
};

} // namespace Http
} // namespace Envoy
