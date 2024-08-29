#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"

#include "source/common/http/codec_helper.h"
#include "source/common/quic/envoy_quic_simulated_watermark_buffer.h"
#include "source/common/quic/envoy_quic_utils.h"

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
#include "source/common/quic/http_datagram_handler.h"
#endif
#include "source/common/quic/quic_filter_manager_connection_impl.h"
#include "source/common/quic/quic_stats_gatherer.h"
#include "source/common/quic/send_buffer_monitor.h"

#include "quiche/http2/adapter/header_validator.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"

namespace Envoy {
namespace Quic {

// Base class for EnvoyQuicServer|ClientStream.
class EnvoyQuicStream : public virtual Http::StreamEncoder,
                        public Http::MultiplexedStreamImplBase,
                        public SendBufferMonitor,
                        public HeaderValidator,
                        protected Logger::Loggable<Logger::Id::quic_stream> {
public:
  // |buffer_limit| is the high watermark of the stream send buffer, and the low
  // watermark will be half of it.
  EnvoyQuicStream(quic::QuicSpdyStream& quic_stream, quic::QuicSession& quic_session,
                  uint32_t buffer_limit, QuicFilterManagerConnectionImpl& filter_manager_connection,
                  std::function<void()> below_low_watermark,
                  std::function<void()> above_high_watermark, Http::Http3::CodecStats& stats,
                  const envoy::config::core::v3::Http3ProtocolOptions& http3_options)
      : Http::MultiplexedStreamImplBase(filter_manager_connection.dispatcher()), stats_(stats),
        http3_options_(http3_options), quic_stream_(quic_stream), quic_session_(quic_session),
        send_buffer_simulation_(buffer_limit / 2, buffer_limit, std::move(below_low_watermark),
                                std::move(above_high_watermark), ENVOY_LOGGER()),
        filter_manager_connection_(filter_manager_connection),
        async_stream_blockage_change_(
            filter_manager_connection.dispatcher().createSchedulableCallback(
                [this]() { switchStreamBlockState(); })) {}

  ~EnvoyQuicStream() override = default;

  // Http::StreamEncoder
  Stream& getStream() override { return *this; }
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Http::MetadataMapVector& metadata_map_vector) override;

  // Http::Stream
  void readDisable(bool disable) override {
    bool status_changed{false};
    if (disable) {
      ++read_disable_counter_;
      if (read_disable_counter_ == 1) {
        status_changed = true;
      }
    } else {
      ASSERT(read_disable_counter_ > 0);
      --read_disable_counter_;
      if (read_disable_counter_ == 0) {
        status_changed = true;
      }
    }

    if (!status_changed) {
      return;
    }

    // If the status transiently changed from unblocked to blocked and then unblocked, the quic
    // stream will be spuriously unblocked and call OnDataAvailable(). This call shouldn't take any
    // effect because any available data should have been processed already upon arrival or they
    // were blocked by some condition other than flow control, i.e. Qpack decoding.
    async_stream_blockage_change_->scheduleCallbackNextIteration();
  }

  void addCallbacks(Http::StreamCallbacks& callbacks) override {
    ASSERT(!local_end_stream_);
    addCallbacksHelper(callbacks);
  }
  void removeCallbacks(Http::StreamCallbacks& callbacks) override {
    removeCallbacksHelper(callbacks);
  }
  uint32_t bufferLimit() const override { return send_buffer_simulation_.highWatermark(); }
  const Network::ConnectionInfoProvider& connectionInfoProvider() override {
    return connection()->connectionInfoProvider();
  }

  Buffer::BufferMemoryAccountSharedPtr account() const override { return buffer_memory_account_; }

  void setAccount(Buffer::BufferMemoryAccountSharedPtr account) override {
    buffer_memory_account_ = account;
  }

  // SendBufferMonitor
  void updateBytesBuffered(uint64_t old_buffered_bytes, uint64_t new_buffered_bytes) override {
    if (new_buffered_bytes == old_buffered_bytes) {
      return;
    }
    // If buffered bytes changed, update stream and session's watermark book
    // keeping.
    if (new_buffered_bytes > old_buffered_bytes) {
      send_buffer_simulation_.checkHighWatermark(new_buffered_bytes);
    } else {
      send_buffer_simulation_.checkLowWatermark(new_buffered_bytes);
    }
    filter_manager_connection_.updateBytesBuffered(old_buffered_bytes, new_buffered_bytes);
  }

  Http::HeaderUtility::HeaderValidationResult
  validateHeader(absl::string_view header_name, absl::string_view header_value) override {
    bool override_stream_error_on_invalid_http_message =
        http3_options_.override_stream_error_on_invalid_http_message().value();
    if (header_validator_.ValidateSingleHeader(header_name, header_value) !=
        http2::adapter::HeaderValidator::HEADER_OK) {
      close_connection_upon_invalid_header_ = !override_stream_error_on_invalid_http_message;
      return Http::HeaderUtility::HeaderValidationResult::REJECT;
    }
    if (header_name == "content-length") {
      size_t content_length = 0;
      Http::HeaderUtility::HeaderValidationResult result =
          Http::HeaderUtility::validateContentLength(
              header_value, override_stream_error_on_invalid_http_message,
              close_connection_upon_invalid_header_, content_length);
      content_length_ = content_length;
      return result;
    }
    ASSERT(!header_name.empty());
    if (Http::HeaderUtility::isPseudoHeader(header_name) && saw_regular_headers_) {
      // If any regular header appears before pseudo headers, the request or response is malformed.
      return Http::HeaderUtility::HeaderValidationResult::REJECT;
    }
    if (!Http::HeaderUtility::isPseudoHeader(header_name)) {
      saw_regular_headers_ = true;
    }
    return Http::HeaderUtility::HeaderValidationResult::ACCEPT;
  }

  absl::string_view responseDetails() override { return details_; }

  const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }

  QuicStatsGatherer* statsGatherer() { return stats_gatherer_.get(); }

protected:
  virtual void switchStreamBlockState() PURE;

  // Needed for ENVOY_STREAM_LOG.
  virtual uint32_t streamId() PURE;
  virtual Network::Connection* connection() PURE;
  // Either reset the stream or close the connection according to
  // should_close_connection and configured http3 options.
  virtual void
  onStreamError(absl::optional<bool> should_close_connection,
                quic::QuicRstStreamErrorCode rst = quic::QUIC_BAD_APPLICATION_PAYLOAD) PURE;

  // TODO(danzh) remove this once QUICHE enforces content-length consistency.
  void updateReceivedContentBytes(size_t payload_length, bool end_stream) {
    received_content_bytes_ += payload_length;
    if (!content_length_.has_value()) {
      return;
    }
    if (received_content_bytes_ > content_length_.value() ||
        (end_stream && received_content_bytes_ != content_length_.value() &&
         !(got_304_response_ && received_content_bytes_ == 0) && !(sent_head_request_))) {
      details_ = Http3ResponseCodeDetailValues::inconsistent_content_length;
      // Reset instead of closing the connection to align with nghttp2.
      onStreamError(false);
    }
  }

  StreamInfo::BytesMeterSharedPtr& mutableBytesMeter() { return bytes_meter_; }

  void encodeTrailersImpl(quiche::HttpHeaderBlock&& trailers);

  // Converts `header_list` into a new `Http::MetadataMap`.
  std::unique_ptr<Http::MetadataMap>
  metadataMapFromHeaderList(const quic::QuicHeaderList& header_list);

  // Returns true if the cumulative limit on METADATA headers has been reached
  // after adding `bytes`.
  bool mustRejectMetadata(size_t bytes) {
    received_metadata_bytes_ += bytes;
    return received_metadata_bytes_ > 1 << 20;
  }

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  // Setting |http_datagram_handler_| enables HTTP Datagram support.
  std::unique_ptr<HttpDatagramHandler> http_datagram_handler_;
#endif
  quiche::QuicheReferenceCountedPointer<QuicStatsGatherer> stats_gatherer_;

  // True once end of stream is propagated to Envoy. Envoy doesn't expect to be
  // notified more than once about end of stream. So once this is true, no need
  // to set it in the callback to Envoy stream any more.
  bool end_stream_decoded_{false};
  // The latest state a QUIC stream blockage state change callback should look at. As
  // more readDisable() calls may happen between the callback is posted and it's
  // executed, the stream might be unblocked and blocked several times. If this
  // counter is 0, the callback should unblock the stream. Otherwise it should
  // block the stream.
  uint32_t read_disable_counter_{0u};

  Http::Http3::CodecStats& stats_;
  const envoy::config::core::v3::Http3ProtocolOptions& http3_options_;
  bool close_connection_upon_invalid_header_{false};
  absl::string_view details_;
  // TODO(kbaichoo): bind the account to the QUIC buffers to enable tracking of
  // memory allocated within QUIC buffers.
  Buffer::BufferMemoryAccountSharedPtr buffer_memory_account_ = nullptr;
  bool got_304_response_{false};
  bool sent_head_request_{false};
  // True if a regular (non-pseudo) HTTP header has been seen before.
  bool saw_regular_headers_{false};

private:
  // QUIC stream and session that this EnvoyQuicStream wraps.
  quic::QuicSpdyStream& quic_stream_;
  quic::QuicSession& quic_session_;

  // Keeps track of bytes buffered in the stream send buffer in QUICHE and reacts
  // upon crossing high and low watermarks.
  // Its high watermark is also the buffer limit of stream read/write filters in
  // HCM.
  // There is no receive buffer simulation because Quic stream's
  // OnBodyDataAvailable() hands all the ready-to-use request data from stream sequencer to HCM
  // directly and buffers them in filters if needed. Itself doesn't buffer request data.
  EnvoyQuicSimulatedWatermarkBuffer send_buffer_simulation_;

  QuicFilterManagerConnectionImpl& filter_manager_connection_;
  // Used to block or unblock stream in the next event loop. QUICHE doesn't like stream blockage
  // state change in its own call stack. And Envoy upstream doesn't like quic stream to be unblocked
  // in its callstack either because the stream will push data right away.
  Event::SchedulableCallbackPtr async_stream_blockage_change_;

  StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};
  absl::optional<size_t> content_length_;
  size_t received_content_bytes_{0};
  http2::adapter::HeaderValidator header_validator_;
  size_t received_metadata_bytes_{0};
};

// Object used for updating a BytesMeter to track bytes sent on a QuicStream since this object was
// constructed.
class IncrementalBytesSentTracker {
public:
  IncrementalBytesSentTracker(const quic::QuicStream& stream, StreamInfo::BytesMeter& bytes_meter,
                              bool update_header_bytes)
      : stream_(stream), bytes_meter_(bytes_meter), update_header_bytes_(update_header_bytes),
        initial_bytes_sent_(totalStreamBytesWritten()) {}

  ~IncrementalBytesSentTracker() {
    if (update_header_bytes_) {
      bytes_meter_.addHeaderBytesSent(incrementalBytesWritten());
    }
    bytes_meter_.addWireBytesSent(incrementalBytesWritten());
  }

private:
  // Returns the number of newly sent bytes since the tracker was constructed.
  uint64_t incrementalBytesWritten() {
    ASSERT(totalStreamBytesWritten() >= initial_bytes_sent_);
    return totalStreamBytesWritten() - initial_bytes_sent_;
  }

  // Returns total number of stream bytes written, including buffered bytes.
  uint64_t totalStreamBytesWritten() const {
    return stream_.stream_bytes_written() + stream_.BufferedDataBytes();
  }

  const quic::QuicStream& stream_;
  StreamInfo::BytesMeter& bytes_meter_;
  bool update_header_bytes_;
  uint64_t initial_bytes_sent_;
};

} // namespace Quic
} // namespace Envoy
