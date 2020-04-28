#pragma once

#include <unordered_map>

#include "envoy/buffer/buffer.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/api_listener.h"
#include "envoy/http/async_client.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/common/thread_synchronizer.h"
#include "common/http/codec_helper.h"

#include "absl/types/optional.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Http {

/**
 * Manages HTTP streams, and provides an interface to interact with them.
 * The Dispatcher executes all stream operations on the provided Event::Dispatcher's event loop.
 */
class Dispatcher : public Logger::Loggable<Logger::Id::http> {
public:
  Dispatcher(std::atomic<envoy_network_t>& preferred_network);

  void ready(Event::Dispatcher& event_dispatcher, ApiListener& api_listener);

  /**
   * Attempts to open a new stream to the remote. Note that this function is asynchronous and
   * opening a stream may fail. The returned handle is immediately valid for use with this API, but
   * there is no guarantee it will ever functionally represent an open stream.
   * @param stream, the stream to start.
   * @param bridge_callbacks, wrapper for callbacks for events on this stream.
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

  Thread::ThreadSynchronizer& synchronizer() { return synchronizer_; }

private:
  class DirectStream;

  /**
   * Notifies caller of async HTTP stream status.
   * Note the HTTP stream is full-duplex, even if the local to remote stream has been ended
   * by sendHeaders/sendData with end_stream=true, sendTrailers, or locallyCloseStream
   * DirectStreamCallbacks can continue to receive events until the remote to local stream is
   * closed, or resetStream is called.
   */
  class DirectStreamCallbacks : public ResponseEncoder, public Logger::Loggable<Logger::Id::http> {
  public:
    DirectStreamCallbacks(DirectStream& direct_stream, envoy_http_callbacks bridge_callbacks,
                          Dispatcher& http_dispatcher);

    void onReset();
    void onCancel();
    void closeRemote(bool end_stream);

    // ResponseEncoder
    void encodeHeaders(const ResponseHeaderMap& headers, bool end_stream) override;
    void encodeData(Buffer::Instance& data, bool end_stream) override;
    void encodeTrailers(const ResponseTrailerMap& trailers) override;
    Stream& getStream() override;
    Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override { return absl::nullopt; }
    // TODO: implement
    void encode100ContinueHeaders(const ResponseHeaderMap&) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
    void encodeMetadata(const MetadataMapVector&) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  private:
    DirectStream& direct_stream_;
    const envoy_http_callbacks bridge_callbacks_;
    absl::optional<envoy_error_code_t> error_code_;
    absl::optional<envoy_data> error_message_;
    absl::optional<int32_t> error_attempt_count_;
    Dispatcher& http_dispatcher_;
  };

  using DirectStreamCallbacksPtr = std::unique_ptr<DirectStreamCallbacks>;

  /**
   * Contains state about an HTTP stream; both in the outgoing direction via an underlying
   * AsyncClient::Stream and in the incoming direction via DirectStreamCallbacks.
   */
  class DirectStream : public Stream,
                       public StreamCallbackHelper,
                       public Logger::Loggable<Logger::Id::http> {
  public:
    DirectStream(envoy_stream_t stream_handle, Dispatcher& http_dispatcher);
    ~DirectStream();

    // Stream
    void addCallbacks(StreamCallbacks& callbacks) override { addCallbacksHelper(callbacks); }
    void removeCallbacks(StreamCallbacks& callbacks) override { removeCallbacksHelper(callbacks); }
    void resetStream(StreamResetReason) override;
    const Network::Address::InstanceConstSharedPtr& connectionLocalAddress() override {
      return parent_.address_;
    }
    // TODO: https://github.com/lyft/envoy-mobile/issues/825
    void readDisable(bool /*disable*/) override {}
    uint32_t bufferLimit() override { return 65000; }

    void closeLocal(bool end_stream);

    /**
     * Return whether a callback should be allowed to continue with execution.
     * This ensures at most one 'terminal' callback is issued for any given stream.
     *
     * @param close, whether the DirectStream should close if it has not closed before.
     * @return bool, whether callbacks on this stream are dispatchable or not.
     */
    bool dispatchable(bool close);

    const envoy_stream_t stream_handle_;
    // https://github.com/lyft/envoy-mobile/pull/616 moved stream cancellation (and its atomic
    // state) from the platform layer to the core layer, here. This change was made to solidify two
    // platform-level implementations into one implementation in the core layer. Moreover, it
    // allowed Envoy Mobile to have test coverage where it didn't before.

    // However, it introduced a subtle race between Dispatcher::resetStream's onCancel and any of
    // encodeHeaders/Data's callbacks that are _not_ terminal. The race happens because the two
    // callbacks are being enqueued onto the same dispatch queue/ran on the same executor by two
    // different threading contexts _after_ the atomic check of the closed_ state happens. This
    // means that they could be serialized in either order; whereas we want to guarantee that _no_
    // callback will be executed after onCancel fires in the application. The lock protects the
    // critical region between the call to dispatchable, and after the call that dispatches the
    // appropriate callback. There should not be much lock contention because most calls will happen
    // from the single-threaded context of the Envoy Main thread (encodeHeaders/Data). Alternative
    // solutions will be considered in: https://github.com/lyft/envoy-mobile/issues/647
    Thread::MutexBasicLockable dispatch_lock_;
    std::atomic<bool> closed_{};
    bool local_closed_{};
    bool hcm_stream_pending_destroy_{};

    // Used to issue outgoing HTTP stream operations.
    RequestDecoder* request_decoder_;
    // Used to receive incoming HTTP stream operations.
    DirectStreamCallbacksPtr callbacks_;
    Dispatcher& parent_;
  };

  using DirectStreamSharedPtr = std::shared_ptr<DirectStream>;

  /**
   * Post a functor to the dispatcher. This is safe cross thread.
   * @param callback, the functor to post.
   */
  void post(Event::PostCb callback);
  DirectStreamSharedPtr getStream(envoy_stream_t stream_handle);
  void cleanup(envoy_stream_t stream_handle);
  void setDestinationCluster(HeaderMap& headers);

  Thread::MutexBasicLockable ready_lock_;
  std::list<Event::PostCb> init_queue_ GUARDED_BY(ready_lock_);
  Event::Dispatcher* event_dispatcher_ GUARDED_BY(ready_lock_){};
  ApiListener* api_listener_ GUARDED_BY(ready_lock_){};
  // std::unordered_map does is not safe for concurrent access. Thus a cross-thread, concurrent find
  // in cancellation (which happens in a platform thread) with an erase (which always happens in the
  // Envoy Main thread) is not safe.
  // TODO: implement a lock-free access scheme here.
  // https://github.com/lyft/envoy-mobile/issues/647
  Thread::MutexBasicLockable streams_lock_;
  // streams_ holds shared_ptr in order to allow cancellation to happen synchronously even though
  // DirectStream cleanup happens asynchronously. This is also done to keep the scope of the
  // streams_lock_ small to make it easier to remove; one could easily use the lock in the
  // Dispatcher::resetStream to avoid using shared_ptrs.
  // @see Dispatcher::resetStream.
  std::unordered_map<envoy_stream_t, DirectStreamSharedPtr> streams_ GUARDED_BY(streams_lock_);
  std::atomic<envoy_network_t>& preferred_network_;
  // Shared synthetic address across DirectStreams.
  Network::Address::InstanceConstSharedPtr address_;
  Thread::ThreadSynchronizer synchronizer_;
};

} // namespace Http
} // namespace Envoy
