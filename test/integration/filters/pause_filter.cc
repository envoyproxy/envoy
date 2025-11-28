#include <string>

#include "envoy/registry/registry.h"

#include "source/common/network/connection_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

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
      absl::WriterMutexLock m(encode_lock_);
      number_of_decode_calls_ref_++;
      // If this is the second stream to decode headers and we're at high watermark. force low
      // watermark state
      if (number_of_decode_calls_ref_ == 2 && connection()->aboveHighWatermark()) {
        connection()->onWriteBufferLowWatermark();
      }
    }
    return PassThroughFilter::decodeData(buf, end_stream);
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& buf, bool end_stream) override {
    if (end_stream) {
      absl::WriterMutexLock m(encode_lock_);
      number_of_encode_calls_ref_++;
      // If this is the first stream to encode headers and we're not at high watermark, force high
      // watermark state.
      if (number_of_encode_calls_ref_ == 1 && !connection()->aboveHighWatermark()) {
        connection()->onWriteBufferHighWatermark();
      }
    }
    return PassThroughFilter::encodeData(buf, end_stream);
  }

  Network::ConnectionImpl* connection() {
    // As long as we're doing horrible things let's do *all* the horrible things.
    // Assert the connection we have is a ConnectionImpl and const cast it so we
    // can force watermark changes.
    const Network::Connection& connection = *decoder_callbacks_->connection();
    return const_cast<Network::ConnectionImpl*>(
        dynamic_cast<const Network::ConnectionImpl*>(&connection));
  }

  absl::Mutex& encode_lock_;
  uint32_t& number_of_encode_calls_ref_;
  uint32_t& number_of_decode_calls_ref_;
};

class TestPauseFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  TestPauseFilterConfig() : EmptyHttpFilterConfig("pause-filter") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      // ABSL_GUARDED_BY insists the lock be held when the guarded variables are passed by
      // reference.
      absl::WriterMutexLock m(encode_lock_);
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
