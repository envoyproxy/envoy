#pragma once

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"

#include "common/http/codec_helper.h"
#include "common/quic/envoy_quic_simulated_watermark_buffer.h"
#include "common/quic/envoy_quic_utils.h"
#include "common/quic/quic_filter_manager_connection_impl.h"
#include "common/quic/send_buffer_monitor.h"

namespace Envoy {
namespace Quic {

// Changes or additions to details should be reflected in
// docs/root/configuration/http/http_conn_man/response_code_details_details.rst
class Http3ResponseCodeDetailValues {
public:
  // Invalid HTTP header field was received and stream is going to be
  // closed.
  static constexpr absl::string_view invalid_http_header = "http3.invalid_header_field";
  // The size of headers (or trailers) exceeded the configured limits.
  static constexpr absl::string_view headers_too_large = "http3.headers_too_large";
  // Envoy was configured to drop requests with header keys beginning with underscores.
  static constexpr absl::string_view invalid_underscore = "http3.unexpected_underscore";
  // The peer refused the stream.
  static constexpr absl::string_view remote_refused = "http3.remote_refuse";
  // The peer reset the stream.
  static constexpr absl::string_view remote_reset = "http3.remote_reset";
};

// Base class for EnvoyQuicServer|ClientStream.
class EnvoyQuicStream : public virtual Http::StreamEncoder,
                        public Http::Stream,
                        public Http::StreamCallbackHelper,
                        public SendBufferMonitor,
                        public HeaderValidator,
                        protected Logger::Loggable<Logger::Id::quic_stream> {
public:
  // |buffer_limit| is the high watermark of the stream send buffer, and the low
  // watermark will be half of it.
  EnvoyQuicStream(uint32_t buffer_limit, QuicFilterManagerConnectionImpl& filter_manager_connection,
                  std::function<void()> below_low_watermark,
                  std::function<void()> above_high_watermark, Http::Http3::CodecStats& stats,
                  const envoy::config::core::v3::Http3ProtocolOptions& http3_options)
      : stats_(stats), http3_options_(http3_options),
        send_buffer_simulation_(buffer_limit / 2, buffer_limit, std::move(below_low_watermark),
                                std::move(above_high_watermark), ENVOY_LOGGER()),
        filter_manager_connection_(filter_manager_connection) {}

  ~EnvoyQuicStream() override = default;

  // Http::StreamEncoder
  Stream& getStream() override { return *this; }

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

    if (status_changed && !in_decode_data_callstack_) {
      // Avoid calling this while decoding data because transient disabling and
      // enabling reading may trigger another decoding data inside the
      // callstack which messes up stream state.
      if (disable) {
        // Block QUIC stream right away. And if there are queued switching
        // state callback, update the desired state as well.
        switchStreamBlockState(true);
        if (unblock_posted_) {
          should_block_ = true;
        }
      } else {
        should_block_ = false;
        if (!unblock_posted_) {
          // If this is the first time unblocking stream is desired, post a
          // callback to do it in next loop. This is because unblocking QUIC
          // stream can lead to immediate upstream encoding.
          unblock_posted_ = true;
          connection()->dispatcher().post([this] {
            unblock_posted_ = false;
            switchStreamBlockState(should_block_);
          });
        }
      }
    }
  }

  void addCallbacks(Http::StreamCallbacks& callbacks) override {
    ASSERT(!local_end_stream_);
    addCallbacksHelper(callbacks);
  }
  void removeCallbacks(Http::StreamCallbacks& callbacks) override {
    removeCallbacksHelper(callbacks);
  }
  uint32_t bufferLimit() override { return send_buffer_simulation_.highWatermark(); }
  const Network::Address::InstanceConstSharedPtr& connectionLocalAddress() override {
    return connection()->addressProvider().localAddress();
  }

  // SendBufferMonitor
  void updateBytesBuffered(size_t old_buffered_bytes, size_t new_buffered_bytes) override {
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
  validateHeader(const std::string& header_name, absl::string_view header_value) override {
    bool override_stream_error_on_invalid_http_message =
        http3_options_.override_stream_error_on_invalid_http_message().value();
    if (header_name == "content-length") {
      return Http::HeaderUtility::validateContentLength(
          header_value, override_stream_error_on_invalid_http_message,
          close_connection_upon_invalid_header_);
    }
    return Http::HeaderUtility::HeaderValidationResult::ACCEPT;
  }

  absl::string_view responseDetails() override { return details_; }

protected:
  virtual void switchStreamBlockState(bool should_block) PURE;

  // Needed for ENVOY_STREAM_LOG.
  virtual uint32_t streamId() PURE;
  virtual Network::Connection* connection() PURE;

  // True once end of stream is propagated to Envoy. Envoy doesn't expect to be
  // notified more than once about end of stream. So once this is true, no need
  // to set it in the callback to Envoy stream any more.
  bool end_stream_decoded_{false};
  uint32_t read_disable_counter_{0u};
  // If true, switchStreamBlockState() should be deferred till this variable
  // becomes false.
  bool in_decode_data_callstack_{false};

  Http::Http3::CodecStats& stats_;
  const envoy::config::core::v3::Http3ProtocolOptions& http3_options_;
  bool close_connection_upon_invalid_header_{false};
  absl::string_view details_;

private:
  // Keeps track of bytes buffered in the stream send buffer in QUICHE and reacts
  // upon crossing high and low watermarks.
  // Its high watermark is also the buffer limit of stream read/write filters in
  // HCM.
  // There is no receive buffer simulation because Quic stream's
  // OnBodyDataAvailable() hands all the ready-to-use request data from stream sequencer to HCM
  // directly and buffers them in filters if needed. Itself doesn't buffer request data.
  EnvoyQuicSimulatedWatermarkBuffer send_buffer_simulation_;

  // True if there is posted unblocking QUIC stream callback. There should be
  // only one such callback no matter how many times readDisable() is called.
  bool unblock_posted_{false};
  // The latest state an unblocking QUIC stream callback should look at. As
  // more readDisable() calls may happen between the callback is posted and it's
  // executed, the stream might be unblocked and blocked several times. Only the
  // latest desired state should be considered by the callback.
  bool should_block_{false};

  QuicFilterManagerConnectionImpl& filter_manager_connection_;
};

} // namespace Quic
} // namespace Envoy
