#include <string>

#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/sleeping_filter.pb.h"
#include "test/integration/filters/sleeping_filter.pb.validate.h"
#include "test/test_common/test_time_system.h"

namespace Envoy {

// A test filter that inserts sleeps during the decode/encode phase. Sleep is induced by invoking a
// thread sleep, and it is done so to simulate a slow/hanging filter that causes the thread
// executing the filter callbacks to block.
class SleepingFilter : public Http::PassThroughFilter {
public:
  // The dynamic_cast from Event::TimeSystem& to Event::TestTimeSystem& is necessary for exposing
  // the sleep API to this filter.
  SleepingFilter(Event::TimeSystem& time_system, Event::TimeSystem::Duration& sleep_duration)
      : PassThroughFilter(), time_system_(dynamic_cast<Event::TestTimeSystem&>(time_system)),
        sleep_duration_(sleep_duration) {}

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) {
    time_system_.sleep(sleep_duration_);
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) {
    time_system_.sleep(sleep_duration_);
    return Http::FilterDataStatus::Continue;
  }

private:
  Event::TestTimeSystem& time_system_;
  Event::TimeSystem::Duration& sleep_duration_;
};

class SleepingFilterFactory : public Extensions::HttpFilters::Common::FactoryBase<
                                  test::integration::filters::SleepingFilterConfig> {
public:
  SleepingFilterFactory() : FactoryBase("sleeping-filter") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::SleepingFilterConfig& proto_config, const std::string&,
      Server::Configuration::FactoryContext& context) {
    Event::TimeSystem::Duration sleep_duration(
        std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(proto_config, sleep_duration)));
    return [&context, &sleep_duration](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(
          std::make_shared<SleepingFilter>(context.dispatcher().timeSystem(), sleep_duration));
    };
  }
};

// perform static registration
static Registry::RegisterFactory<SleepingFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
