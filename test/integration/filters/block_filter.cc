#include <chrono>
#include <thread>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/block_filter.pb.h"
#include "test/integration/filters/block_filter.pb.validate.h"

#include "absl/time/time.h"

namespace Envoy {

/**
 * A test filter that blocks the thread for a configured duration in decodeHeaders.
 * This is useful for triggering watchdog events in integration tests.
 * Note that the filter uses real thread sleep to make sure the watchdog thread
 * picks up the issue).
 */
class BlockFilter : public Http::PassThroughFilter {
public:
  BlockFilter(std::chrono::milliseconds block_duration) : block_duration_(block_duration) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    if (block_duration_.count() > 0) {
      // Blocking the thread synchronously to simulate a non-responsive thread.
      // We use sleep_for here instead of advanceTimeWait because the watchdog runs on its own
      // thread and needs real time to elapse to trigger a miss/megamiss event when it checks
      // on the responsiveness of the worker threads.
      ENVOY_LOG_MISC(info, "BlockFilter: starting sleep for {}ms", block_duration_.count());
      absl::SleepFor(absl::Milliseconds(block_duration_.count()));
      ENVOY_LOG_MISC(info, "BlockFilter: finished sleep");
    }
    return Http::FilterHeadersStatus::Continue;
  }

private:
  const std::chrono::milliseconds block_duration_;
};

/**
 * Factory for BlockFilter.
 */
class BlockFilterFactory : public Extensions::HttpFilters::Common::DualFactoryBase<
                               test::integration::filters::BlockFilterConfig> {
public:
  BlockFilterFactory() : DualFactoryBase("block-filter") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const test::integration::filters::BlockFilterConfig& proto_config, const std::string&,
      DualInfo, Server::Configuration::ServerFactoryContext&) override {
    auto block_duration = std::chrono::milliseconds(
        DurationUtil::durationToMilliseconds(proto_config.block_duration()));
    return [block_duration](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<BlockFilter>(block_duration));
    };
  }
};

REGISTER_FACTORY(BlockFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Envoy
