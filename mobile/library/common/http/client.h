#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/http/api_listener.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/logger.h"
#include "source/common/http/codec_helper.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
#include "library/common/event/provisional_dispatcher.h"
#include "library/common/network/synthetic_address_impl.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Http {

/**
 * All http client stats. @see stats_macros.h
 */
#define ALL_HTTP_CLIENT_STATS(COUNTER)                                                             \
  COUNTER(stream_success)                                                                          \
  COUNTER(stream_failure)                                                                          \
  COUNTER(stream_cancel)

/**
 * Struct definition for client stats. @see stats_macros.h
 */
struct HttpClientStats {
  ALL_HTTP_CLIENT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Manages HTTP streams, and provides an interface to interact with them.
 */
class Client : public Logger::Loggable<Logger::Id::http> {
public:
  Client(ApiListener& api_listener, Event::ProvisionalDispatcher& dispatcher, Stats::Scope& scope,
         std::atomic<envoy_network_t>& preferred_network, Random::RandomGenerator& random)
      : api_listener_(api_listener), dispatcher_(dispatcher),
        stats_(HttpClientStats{ALL_HTTP_CLIENT_STATS(POOL_COUNTER_PREFIX(scope, "http.client."))}),
        preferred_network_(preferred_network),
        address_(std::make_shared<Network::Address::SyntheticAddressImpl>()), random_(random) {}

  /**
   * Attempts to open a new stream to the remote. Note that this function is asynchronous and
   * opening a stream may fail. The returned handle is immediately valid for use with this API, but
   * there is no guarantee it will ever functionally represent an open stream.
   * @param stream, the stream to start.
   * @param bridge_callbacks, wrapper for callbacks for events on this stream.
   * @param explicit_flow_control, whether the stream will require explicit flow control.
   */
  void startStream(envoy_stream_t stream, envoy_http_callbacks bridge_callbacks,
                   bool explicit_flow_control);

  /**
   * Send headers over an open HTTP stream. This method can be invoked once and needs to be called
   * before send_data.
   * @param stream, the stream to send headers over.
   * @param headers, the headers to send.
   * @param end_stream, indicates whether to close the stream locally after sending this frame.
   */
  void sendHeaders(envoy_stream_t stream, envoy_headers headers, bool end_stream);

  /**
   * Notify the stream that the caller is ready to receive more data from the response stream. Only
   * used in explicit flow control mode.
   * @param bytes_to_read, the quantity of data the caller is prepared to process.
   */
  void readData(envoy_stream_t stream, size_t bytes_to_read);

  /**
   * Send data over an open HTTP stream. This method can be invoked multiple times.
   * @param stream, the stream to send data over.
   * @param data, the data to send.
   * @param end_stream, indicates whether to close the stream locally after sending this frame.
   */
  void sendData(envoy_stream_t stream, envoy_data data, bool end_stream);

  /**
   * Send metadata over an HTTP stream. This method can be invoked multiple times.
   * @param stream, the stream to send metadata over.
   * @param metadata, the metadata to send.
   */
  void sendMetadata(envoy_stream_t stream, envoy_headers metadata);

  /**
   * Send trailers over an open HTTP stream. This method can only be invoked once per stream.
   * Note that this method implicitly closes the stream locally.
   * @param stream, the stream to send trailers over.
   * @param trailers, the trailers to send.
   */
  void sendTrailers(envoy_stream_t stream, envoy_headers trailers);

  /**
   * Reset an open HTTP stream. This operation closes the stream locally, and remote.
   * No further operations are valid on the stream.
   * @param stream, the stream to reset.
   */
  void cancelStream(envoy_stream_t stream);

  const HttpClientStats& stats() const;
  Event::ScopeTracker& scopeTracker() const { return dispatcher_; }

  // Used to fill response code details for streams that are cancelled via cancelStream.
  const std::string& getCancelDetails() {
    CONSTRUCT_ON_FIRST_USE(std::string, "client cancelled stream");
  }

private:
  class DirectStream;
  friend class ClientTest;
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
                          Client& http_client);

    void closeStream();
    void onComplete();
    void onCancel();
    void onError();
    // Remove the stream and clear up state if possible, else set up deferred
    // removal path.
    void removeStream();

    // ResponseEncoder
    void encodeHeaders(const ResponseHeaderMap& headers, bool end_stream) override;
    void encodeData(Buffer::Instance& data, bool end_stream) override;
    void encodeTrailers(const ResponseTrailerMap& trailers) override;
    Stream& getStream() override { return direct_stream_; }
    Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override { return absl::nullopt; }
    void encode100ContinueHeaders(const ResponseHeaderMap&) override {
      // TODO(goaway): implement?
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
    bool streamErrorOnInvalidHttpMessage() const override { return false; }

    void encodeMetadata(const MetadataMapVector&) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

    void onHasBufferedData() { direct_stream_.runHighWatermarkCallbacks(); }
    void onBufferedDataDrained() { direct_stream_.runLowWatermarkCallbacks(); }

    // To be called by mobile library when in explicit flow control mode and more data is wanted.
    // If bytes are available, the bytes available (up to the limit of
    // bytes_to_send) will be shipped the bridge immediately.
    //
    // If no bytes are available, the next time data is received from the
    // network, up to bytes_to_send bytes will be shipped to the bridge.
    //
    // Bytes will only be sent up once, even if the bytes available are fewer
    // than bytes_to_send.
    void resumeData(int32_t bytes_to_send);

  private:
    bool hasBufferedData() { return response_data_.get() && response_data_->length() != 0; }

    void sendDataToBridge(Buffer::Instance& data, bool end_stream);
    void sendTrailersToBridge(const ResponseTrailerMap& trailers);
    envoy_stream_intel streamIntel();

    DirectStream& direct_stream_;
    const envoy_http_callbacks bridge_callbacks_;
    Client& http_client_;
    absl::optional<envoy_error_code_t> error_code_;
    absl::optional<envoy_data> error_message_;
    absl::optional<int32_t> error_attempt_count_;
    bool success_{};
    // Buffered response data when in explicit flow control mode.
    Buffer::InstancePtr response_data_;
    ResponseTrailerMapPtr response_trailers_;
    // True if the bridge should operate in explicit flow control mode, and only send
    // data when it is requested by the caller.
    bool explicit_flow_control_{};
    // Set true when the response headers have been forwarded to the bridge.
    bool response_headers_forwarded_{};
    // Called in closeStream() to communicate that the end of the stream has
    // been received by the DirectStreamCallbacks.
    bool remote_end_stream_received_{};
    // Set true when the end stream has been forwarded to the bridge.
    bool remote_end_stream_forwarded_{};
    uint32_t bytes_to_send_{};
  };

  using DirectStreamCallbacksPtr = std::unique_ptr<DirectStreamCallbacks>;

  /**
   * Contains state about an HTTP stream; both in the outgoing direction via an underlying
   * AsyncClient::Stream and in the incoming direction via DirectStreamCallbacks.
   */
  class DirectStream : public Stream,
                       public StreamCallbackHelper,
                       public ScopeTrackedObject,
                       public Logger::Loggable<Logger::Id::http> {
  public:
    DirectStream(envoy_stream_t stream_handle, Client& http_client);
    ~DirectStream();

    // Stream
    void addCallbacks(StreamCallbacks& callbacks) override { addCallbacksHelper(callbacks); }
    void removeCallbacks(StreamCallbacks& callbacks) override { removeCallbacksHelper(callbacks); }
    void resetStream(StreamResetReason) override;
    const Network::Address::InstanceConstSharedPtr& connectionLocalAddress() override {
      return parent_.address_;
    }
    absl::string_view responseDetails() override { return response_details_; }
    // TODO: https://github.com/lyft/envoy-mobile/issues/825
    void readDisable(bool /*disable*/) override {}
    uint32_t bufferLimit() override { return 65000; }
    // Not applicable
    void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {
      PANIC("buffer accounts unsupported");
    }
    void setFlushTimeout(std::chrono::milliseconds) override {}

    // ScopeTrackedObject
    void dumpState(std::ostream& os, int indent_level = 0) const override;

    void setResponseDetails(absl::string_view response_details) {
      response_details_ = response_details;
    }

    const envoy_stream_t stream_handle_;

    // Used to issue outgoing HTTP stream operations.
    RequestDecoder* request_decoder_;
    // Used to receive incoming HTTP stream operations.
    DirectStreamCallbacksPtr callbacks_;
    Client& parent_;
    // Response details used by the connection manager.
    absl::string_view response_details_;
    // True if the bridge should operate in explicit flow control mode.
    //
    // In this mode only one callback can be sent to the bridge until more is
    // asked for. When a response is started this will either allow headers or an
    // error to be sent up. Body, trailers, or further errors will not be sent
    // until resumeData is called. This, combined with standard Envoy flow control push
    // back, avoids excessive buffering of response bodies if the response body is
    // read faster than the mobile caller can process it.
    bool explicit_flow_control_ = false;
  };

  using DirectStreamSharedPtr = std::shared_ptr<DirectStream>;

  // Used to deferredDelete the ref count of the DirectStream owned by streams_ while still
  // maintaining a container of DirectStreamSharedPtr. Using deferredDelete is important due to the
  // necessary ordering of ActiveStream deletion w.r.t DirectStream deletion; the former needs to be
  // destroyed first. Using post to defer delete the DirectStream provides no ordering guarantee per
  // envoy/source/common/event/libevent.h Maintaining a container of DirectStreamSharedPtr is
  // important because Client::resetStream is initiated by a platform thread.
  struct DirectStreamWrapper : public Event::DeferredDeletable {
    DirectStreamWrapper(DirectStreamSharedPtr stream) : stream_(stream) {}

  private:
    const DirectStreamSharedPtr stream_;
  };

  using DirectStreamWrapperPtr = std::unique_ptr<DirectStreamWrapper>;

  enum GetStreamFilters {
    // If a stream has been finished from upstream, but stream completion has
    // not yet been communicated, the downstream mobile library should not be
    // allowed to access the stream. getStream takes an argument to ensure that
    // the mobile client won't do things like send further request data for
    // streams in this state.
    ALLOW_ONLY_FOR_OPEN_STREAMS,
    // If a stream has been finished from upstream, make sure that getStream
    // will continue to work for functions such as resumeData (pushing that data
    // to the mobile library) and cancelStream (the client not wanting further
    // data for the stream).
    ALLOW_FOR_ALL_STREAMS,
  };
  DirectStreamSharedPtr getStream(envoy_stream_t stream_handle, GetStreamFilters filters);
  void removeStream(envoy_stream_t stream_handle);
  void setDestinationCluster(RequestHeaderMap& headers, bool alternate);

  ApiListener& api_listener_;
  Event::ProvisionalDispatcher& dispatcher_;
  HttpClientStats stats_;
  // The set of open streams, which can safely have request data sent on them
  // or response data received.
  absl::flat_hash_map<envoy_stream_t, DirectStreamSharedPtr> streams_;
  // The set of closed streams, where end stream has been received from upstream
  // but not yet communicated to the mobile library.
  absl::flat_hash_map<envoy_stream_t, DirectStreamSharedPtr> closed_streams_;
  std::atomic<envoy_network_t>& preferred_network_;
  // Shared synthetic address across DirectStreams.
  Network::Address::InstanceConstSharedPtr address_;
  Random::RandomGenerator& random_;
};

using ClientPtr = std::unique_ptr<Client>;

} // namespace Http
} // namespace Envoy
