#include <string>

#include "envoy/registry/registry.h"

#include "common/network/connection_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/test_common/utility.h"

namespace Envoy {

// This filter exists to synthetically test network backup by faking TCP
// connection back-up when an encode is finished, blocking and unblocking
// randomly.
class RandomPauseFilter : public Http::PassThroughFilter {
public:
  RandomPauseFilter(absl::Mutex& rand_lock, TestRandomGenerator& rng)
      : rand_lock_(rand_lock), rng_(rng) {}

  Http::FilterDataStatus encodeData(Buffer::Instance& buf, bool end_stream) override {
    absl::WriterMutexLock m(&rand_lock_);
    uint64_t random = rng_.random();
    // Roughly every 5th encode (5 being arbitrary) swap the watermark state.
    if (random % 5 == 0) {
      if (connection()->aboveHighWatermark()) {
        connection()->onWriteBufferLowWatermark();
      } else {
        connection()->onWriteBufferHighWatermark();
      }
    }
    return Http::PassThroughFilter::encodeData(buf, end_stream);
  }

  Network::ConnectionImpl* connection() {
    // As long as we're doing horrible things let's do *all* the horrible things.
    // Assert the connection we have is a ConnectionImpl and const cast it so we
    // can force watermark changes.
    auto conn_impl = dynamic_cast<const Network::ConnectionImpl*>(decoder_callbacks_->connection());
    return const_cast<Network::ConnectionImpl*>(conn_impl);
  }

  absl::Mutex& rand_lock_;
  TestRandomGenerator& rng_;
};

class RandomPauseFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  RandomPauseFilterConfig() : EmptyHttpFilterConfig("random-pause-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [&](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      absl::WriterMutexLock m(&rand_lock_);
      if (rng_ == nullptr) {
        // Lazily create to ensure the test seed is set.
        rng_ = std::make_unique<TestRandomGenerator>();
      }
      callbacks.addStreamFilter(std::make_shared<::Envoy::RandomPauseFilter>(rand_lock_, *rng_));
    };
  }

  absl::Mutex rand_lock_;
  std::unique_ptr<TestRandomGenerator> rng_ ABSL_GUARDED_BY(rand_lock_);
};

// perform static registration
static Registry::RegisterFactory<RandomPauseFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
