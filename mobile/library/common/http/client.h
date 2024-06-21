#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/http/api_listener.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/logger.h"
#include "source/common/http/codec_helper.h"
#include "source/common/network/socket_impl.h"
#include "source/common/stats/timespan_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
#include "library/common/engine_types.h"
#include "library/common/event/provisional_dispatcher.h"
#include "library/common/network/synthetic_address_impl.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Http {

/**
 * All http client stats. @see stats_macros.h
 */
#define ALL_HTTP_CLIENT_STATS(COUNTER, HISTOGRAM)                                                  \
  COUNTER(stream_success)                                                                          \
  COUNTER(stream_failure)                                                                          \
  COUNTER(stream_cancel)                                                                           \
  HISTOGRAM(on_headers_callback_latency, Milliseconds)                                             \
  HISTOGRAM(on_data_callback_latency, Milliseconds)                                                \
  HISTOGRAM(on_trailers_callback_latency, Milliseconds)                                            \
  HISTOGRAM(on_complete_callback_latency, Milliseconds)                                            \
  HISTOGRAM(on_cancel_callback_latency, Milliseconds)                                              \
  HISTOGRAM(on_error_callback_latency, Milliseconds)

/**
 * Struct definition for client stats. @see stats_macros.h
 */
struct HttpClientStats {
  ALL_HTTP_CLIENT_STATS(GENERATE_COUNTER_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

/**
 * Manages HTTP streams, and provides an interface to interact with them.
 */
class Client : public Logger::Loggable<Logger::Id::http> {
public:
  Client(ApiListenerPtr&& api_listener, Event::ProvisionalDispatcher& dispatcher,
         Stats::Scope& scope, Random::RandomGenerator& random)
      : api_listener_(std::move(api_listener)), dispatcher_(dispatcher),
        stats_(
            HttpClientStats{ALL_HTTP_CLIENT_STATS(POOL_COUNTER_PREFIX(scope, "http.client."),
                                                  POOL_HISTOGRAM_PREFIX(scope, "http.client."))}),
        address_provider_(std::make_shared<Network::Address::SyntheticAddressImpl>(), nullptr),
        random_(random) {}

  /**
   * Attempts to open a new stream to the remote. Note that this function is asynchronous and
   * opening a stream may fail. The returned handle is immediately valid for use with this API, but
   * there is no guarantee it will ever functionally represent an open stream.
   * @param stream, the stream to start.
   * @param stream_callbacks, the callbacks for events on this stream.
   * @param explicit_flow_control, whether the stream will require explicit flow control.
   */
  void startStream(envoy_stream_t stream, EnvoyStreamCallbacks&& stream_callbacks,
                   bool explicit_flow_control);

  /**
   * Send headers over an open HTTP stream. This method can be invoked once and needs to be called
   * before send_data.
   *
   * @param stream the stream to send headers over.
   * @param headers the headers to send.
   * @param end_stream indicates whether to close the stream locally after sending this frame.
   */
  void sendHeaders(envoy_stream_t stream, RequestHeaderMapPtr headers, bool end_stream);

  /**
   * Notify the stream that the caller is ready to receive more data from the response stream. Only
   * used in explicit flow control mode.
   * @param bytes_to_read, the quantity of data the caller is prepared to process.
   */
  void readData(envoy_stream_t stream, size_t bytes_to_read);

  /**
   * Send data over an open HTTP stream. This method can be invoked multiple times.
   * @param stream the stream to send data over.
   * @param buffer the data to send.
   * @param end_stream indicates whether to close the stream locally after sending this frame.
   */
  void sendData(envoy_stream_t stream, Buffer::InstancePtr buffer, bool end_stream);

  /**
   * Send metadata over an HTTP stream. This method can be invoked multiple times.
   * @param stream, the stream to send metadata over.
   * @param metadata, the metadata to send.
   */
  void sendMetadata(envoy_stream_t stream, envoy_headers metadata);

  /**
   * Send trailers over an open HTTP stream. This method can only be invoked once per stream.
   * Note that this method implicitly closes the stream locally.
   *
   * @param stream the stream to send trailers over.
   * @param trailers the trailers to send.
   */
  void sendTrailers(envoy_stream_t stream, RequestTrailerMapPtr trailers);

  /**
   * Reset an open HTTP stream. This operation closes the stream locally, and remote.
   * No further operations are valid on the stream.
   * @param stream, the stream to reset.
   */
  void cancelStream(envoy_stream_t stream);

  const HttpClientStats& stats() const;
  Event::ScopeTracker& scopeTracker() const { return dispatcher_; }

  TimeSource& timeSource() { return dispatcher_.timeSource(); }

  // Used to fill response code details for streams that are cancelled via cancelStream.
  const std::string& getCancelDetails() {
    CONSTRUCT_ON_FIRST_USE(std::string, "client_cancelled_stream");
  }

  void shutdownApiListener() { api_listener_.reset(); }

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
    DirectStreamCallbacks(DirectStream& direct_stream, EnvoyStreamCallbacks&& stream_callbacks,
                          Client& http_client);
    virtual ~DirectStreamCallbacks();

    void closeStream(bool end_stream = true);
    void onComplete();
    void onCancel();
    void onError();
    void onSendWindowAvailable();

    // ResponseEncoder
    void encodeHeaders(const ResponseHeaderMap& headers, bool end_stream) override;
    void encodeData(Buffer::Instance& data, bool end_stream) override;
    void encodeTrailers(const ResponseTrailerMap& trailers) override;
    Stream& getStream() override { return direct_stream_; }
    Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override { return absl::nullopt; }
    void encode1xxHeaders(const ResponseHeaderMap&) override {
      IS_ENVOY_BUG("Unexpected 100 continue"); // proxy_100_continue_ false by default.
    }
    bool streamErrorOnInvalidHttpMessage() const override { return false; }
    void setRequestDecoder(RequestDecoder& /*decoder*/) override{};
    void setDeferredLoggingHeadersAndTrailers(Http::RequestHeaderMapConstSharedPtr,
                                              Http::ResponseHeaderMapConstSharedPtr,
                                              Http::ResponseTrailerMapConstSharedPtr,
                                              StreamInfo::StreamInfo&) override {}

    void encodeMetadata(const MetadataMapVector&) override { IS_ENVOY_BUG("Unexpected metadata"); }

    void onHasBufferedData();
    void onBufferedDataDrained();

    // To be called by mobile library when in explicit flow control mode and more data is wanted.
    // If bytes are available, the bytes available (up to the limit of
    // bytes_to_send) will be shipped the bridge immediately.
    //
    // If no bytes are available, the next time data is received from the
    // network, up to bytes_to_send bytes will be shipped to the bridge.
    //
    // Bytes will only be sent up once, even if the bytes available are fewer
    // than bytes_to_send.
    void resumeData(size_t bytes_to_send);

    void latchError();

  private:
    bool hasDataToSend() {
      return (
          (response_data_ && response_data_->length() != 0) ||
          (remote_end_stream_received_ && !remote_end_stream_forwarded_ && !response_trailers_));
    }

    void sendDataToBridge(Buffer::Instance& data, bool end_stream);
    void sendTrailersToBridge(const ResponseTrailerMap& trailers);
    void sendErrorToBridge();
    envoy_stream_intel streamIntel();
    envoy_final_stream_intel& finalStreamIntel();

    DirectStream& direct_stream_;
    EnvoyStreamCallbacks stream_callbacks_;
    Client& http_client_;
    absl::optional<EnvoyError> error_;
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
    size_t bytes_to_send_{};
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
    CodecEventCallbacks* registerCodecEventCallbacks(CodecEventCallbacks* codec_callbacks) override;

    void resetStream(StreamResetReason) override;
    Network::ConnectionInfoProvider& connectionInfoProvider() override {
      return parent_.address_provider_;
    }
    absl::string_view responseDetails() override { return response_details_; }
    // This is called any time upstream buffers exceed the configured flow
    // control limit, to attempt halt the flow of data from the mobile client
    // or to resume the flow of data when buffers have been drained.
    //
    // It only has an effect in explicit flow control mode, where when all buffers are drained,
    // on_send_window_available callbacks are called.
    void readDisable(bool disable) override;
    uint32_t bufferLimit() const override {
      // 1Mb
      return 1024000;
    }
    // Not applicable
    void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {
      // Acounting became default in https://github.com/envoyproxy/envoy/pull/17702 but is a no=op.
    }
    void setFlushTimeout(std::chrono::milliseconds) override {}

    Buffer::BufferMemoryAccountSharedPtr account() const override { return nullptr; }

    const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }

    // ScopeTrackedObject
    void dumpState(std::ostream& os, int indent_level = 0) const override;

    void setResponseDetails(absl::string_view response_details) {
      response_details_ = response_details;
    }

    // Latches stream information as it may not be available when accessed.
    void saveLatestStreamIntel();

    // Latches latency info from stream info before it goes away.
    void saveFinalStreamIntel();

    // Various signals to propagate to the adapter.
    enum class AdapterSignal { EncodeComplete, Error, Cancel };

    // Used to notify adapter of stream's completion.
    void notifyAdapter(AdapterSignal signal) {
      if (codec_callbacks_) {
        switch (signal) {
        case AdapterSignal::EncodeComplete:
          codec_callbacks_->onCodecEncodeComplete();
          break;
        case AdapterSignal::Error:
          FALLTHRU;
        case AdapterSignal::Cancel:
          codec_callbacks_->onCodecLowLevelReset();
        }
        registerCodecEventCallbacks(nullptr);
      }
    }

    OptRef<RequestDecoder> requestDecoder() {
      if (!request_decoder_) {
        return {};
      }
      ENVOY_BUG(request_decoder_->get(), "attempting to access deleted decoder");
      return request_decoder_->get();
    }

    const envoy_stream_t stream_handle_;

    // Used to issue outgoing HTTP stream operations.
    RequestDecoderHandlePtr request_decoder_;
    // Used to receive incoming HTTP stream operations.
    DirectStreamCallbacksPtr callbacks_;
    Client& parent_;
    // Used to communicate with the HTTP Connection Manager that
    // it can destroy the active stream.
    CodecEventCallbacks* codec_callbacks_{nullptr};
    // Response details used by the connection manager.
    absl::string_view response_details_;
    // Tracks read disable calls. Different buffers can call read disable, and
    // the stack should not consider itself "ready to write" until all
    // read-disable calls have been unwound.
    uint32_t read_disable_count_{};
    // Set true in explicit flow control mode if the library has sent body data and may want to
    // send more when buffer is available.
    bool wants_write_notification_{};
    // True if the bridge should operate in explicit flow control mode.
    //
    // In this mode only one callback can be sent to the bridge until more is
    // asked for. When a response is started this will either allow headers or an
    // error to be sent up. Body, trailers, or further errors will not be sent
    // until resumeData is called. This, combined with standard Envoy flow control push
    // back, avoids excessive buffering of response bodies if the response body is
    // read faster than the mobile caller can process it.
    bool explicit_flow_control_ = false;
    // Latest intel data retrieved from the StreamInfo.
    envoy_stream_intel stream_intel_{-1, -1, 0, 0};
    envoy_final_stream_intel envoy_final_stream_intel_{-1, -1, -1, -1, -1, -1, -1, -1,
                                                       -1, -1, -1, 0,  0,  0,  0,  -1};
    StreamInfo::BytesMeterSharedPtr bytes_meter_;
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

  enum class GetStreamFilters {
    // If a stream has been finished from upstream, but stream completion has
    // not yet been communicated, the downstream mobile library should not be
    // allowed to access the stream. getStream takes an argument to ensure that
    // the mobile client won't do things like send further request data for
    // streams in this state.
    AllowOnlyForOpenStreams,
    // If a stream has been finished from upstream, make sure that getStream
    // will continue to work for functions such as resumeData (pushing that data
    // to the mobile library) and cancelStream (the client not wanting further
    // data for the stream).
    AllowForAllStreams,
  };
  DirectStreamSharedPtr getStream(envoy_stream_t stream_handle, GetStreamFilters filters);
  void removeStream(envoy_stream_t stream_handle);
  void setDestinationCluster(RequestHeaderMap& headers);

  ApiListenerPtr api_listener_;
  Event::ProvisionalDispatcher& dispatcher_;
  Event::SchedulableCallbackPtr scheduled_callback_;
  HttpClientStats stats_;
  // The set of open streams, which can safely have request data sent on them
  // or response data received.
  absl::flat_hash_map<envoy_stream_t, DirectStreamSharedPtr> streams_;
  // The set of closed streams, where end stream has been received from upstream
  // but not yet communicated to the mobile library.
  absl::flat_hash_map<envoy_stream_t, DirectStreamSharedPtr> closed_streams_;
  // Shared synthetic address providers across DirectStreams.
  Network::ConnectionInfoSetterImpl address_provider_;
  Random::RandomGenerator& random_;
};

using ClientPtr = std::unique_ptr<Client>;

} // namespace Http
} // namespace Envoy
