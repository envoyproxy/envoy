#include <string>

#include "envoy/registry/registry.h"

#include "common/network/connection_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "extensions/quic_listeners/quiche/quic_filter_manager_connection_impl.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// This filter exists to synthetically test network backup by faking TCP
// connection back-up when an encode is finished, and unblocking it when the
// next stream starts to decode headers.
// Allows regression tests for https://github.com/envoyproxy/envoy/issues/4541
class TestPauseFilter : public Http::PassThroughFilter {
public:
  // Pass in a some global filter state to ensure the Network::Connection is
  // blocked and unblocked exactly once.
  TestPauseFilter(absl::Mutex& encode_lock, uint32_t& number_of_encode_calls_ref,
                  uint32_t& number_of_decode_calls_ref)
      : encode_lock_(encode_lock), number_of_encode_calls_ref_(number_of_encode_calls_ref),
        number_of_decode_calls_ref_(number_of_decode_calls_ref) {}

  Http::FilterDataStatus decodeData(Buffer::Instance& buf, bool end_stream) override {
    if (end_stream) {
      absl::WriterMutexLock m(&encode_lock_);
      number_of_decode_calls_ref_++;
      // If this is the second stream to decode headers and we're at high watermark. force low
      // watermark state
      if (number_of_decode_calls_ref_ == 2 &&
          decoder_callbacks_->connection()->aboveHighWatermark()) {
        mockConnectionBelowLowWatermark();
      }
    }
    return PassThroughFilter::decodeData(buf, end_stream);
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& buf, bool end_stream) override {
    if (end_stream) {
      absl::WriterMutexLock m(&encode_lock_);
      number_of_encode_calls_ref_++;
      // If this is the first stream to encode headers and we're not at high watermark, force high
      // watermark state.
      if (number_of_encode_calls_ref_ == 1 &&
          !decoder_callbacks_->connection()->aboveHighWatermark()) {
        mockConnectionAboveHighWatermark();
      }
    }
    return PassThroughFilter::encodeData(buf, end_stream);
  }

  void mockConnectionAboveHighWatermark() {
    // As long as we're doing horrible things let's do *all* the horrible things.
    // Assert the connection we have is a ConnectionImpl or QuicFilterManagerConnectionImpl and
    // const cast it so we can force watermark changes.
    auto conn_impl = dynamic_cast<const Network::ConnectionImpl*>(decoder_callbacks_->connection());
    if (conn_impl != nullptr) {
      const_cast<Network::ConnectionImpl*>(conn_impl)->onWriteBufferHighWatermark();
      return;
    }
    // If transport protocol is QUIC, simulate connection buffer above watermark differently.
    auto quic_connection = const_cast<Quic::QuicFilterManagerConnectionImpl*>(
        dynamic_cast<const Quic::QuicFilterManagerConnectionImpl*>(
            decoder_callbacks_->connection()));
    quic_connection->write_buffer_watermark_simulation_.checkHighWatermark(
        quic_connection->write_buffer_watermark_simulation_.highWatermark() + 1u);
  }

  void mockConnectionBelowLowWatermark() {
    // As long as we're doing horrible things let's do *all* the horrible things.
    // Assert the connection we have is a ConnectionImpl or QuicFilterManagerConnectionImpl and
    // const cast it so we can force watermark changes.
    auto conn_impl = dynamic_cast<const Network::ConnectionImpl*>(decoder_callbacks_->connection());
    if (conn_impl != nullptr) {
      const_cast<Network::ConnectionImpl*>(conn_impl)->onWriteBufferHighWatermark();
      return;
    }
    // If transport protocol is QUIC, simulate connection buffer below watermark differently.
    auto quic_connection = const_cast<Quic::QuicFilterManagerConnectionImpl*>(
        dynamic_cast<const Quic::QuicFilterManagerConnectionImpl*>(
            decoder_callbacks_->connection()));
    quic_connection->write_buffer_watermark_simulation_.checkLowWatermark(
        quic_connection->write_buffer_watermark_simulation_.highWatermark() / 2 - 1u);
  }

  absl::Mutex& encode_lock_;
  uint32_t& number_of_encode_calls_ref_;
  uint32_t& number_of_decode_calls_ref_;
};

class TestPauseFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  TestPauseFilterConfig() : EmptyHttpFilterConfig("pause-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      // ABSL_GUARDED_BY insists the lock be held when the guarded variables are passed by
      // reference.
      absl::WriterMutexLock m(&encode_lock_);
      callbacks.addStreamFilter(std::make_shared<::Envoy::TestPauseFilter>(
          encode_lock_, number_of_encode_calls_, number_of_decode_calls_));
    };
  }

  absl::Mutex encode_lock_;
  uint32_t number_of_encode_calls_ ABSL_GUARDED_BY(encode_lock_) = 0;
  uint32_t number_of_decode_calls_ ABSL_GUARDED_BY(encode_lock_) = 0;
};

// perform static registration
static Registry::RegisterFactory<TestPauseFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
