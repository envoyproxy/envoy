#include <string>

#include "envoy/registry/registry.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/sleeping_filter.pb.h"
#include "test/integration/filters/sleeping_filter.pb.validate.h"
#include "test/test_common/test_time_system.h"

namespace Envoy {

// A test filter that inserts sleeps during the decode/encode phase.
class SleepingFilter : public Http::PassThroughFilter {
public:
  // The dynamic_cast from Event::TimeSystem& to Event::TestTimeSystem& is necessary for exposing
  // the sleep API to this filter.
  SleepingFilter(Event::TimeSystem& time_system, uint64_t sleep_ms)
      : PassThroughFilter(), time_system_(dynamic_cast<Event::TestTimeSystem&>(time_system)),
        sleep_ms_(sleep_ms) {}

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) {
    time_system_.sleep(std::chrono::milliseconds(sleep_ms_));
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) {
    time_system_.sleep(std::chrono::milliseconds(sleep_ms_));
    return Http::FilterDataStatus::Continue;
  }

private:
  Event::TestTimeSystem& time_system_;
  uint64_t sleep_ms_;
};

class SleepingFilterFactory : public Extensions::HttpFilters::Common::FactoryBase<
                                  test::integration::filters::SleepingFilterConfig> {
public:
  SleepingFilterFactory() : FactoryBase("sleeping-filter") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::SleepingFilterConfig& proto_config, const std::string&,
      Server::Configuration::FactoryContext& context) {
    uint64_t sleep_ms = proto_config.sleep_ms();
    return [&context, sleep_ms](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(
          std::make_shared<SleepingFilter>(context.dispatcher().timeSystem(), sleep_ms));
    };
  }
};

// perform static registration
static Registry::RegisterFactory<SleepingFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
