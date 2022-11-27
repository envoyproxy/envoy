#include "library/common/extensions/filters/http/test_event_tracker/config.h"

#include "library/common/extensions/filters/http/test_event_tracker/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestEventTracker {

Http::FilterFactoryCb TestEventTrackerFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::test_event_tracker::TestEventTracker&
        proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {

  auto config = std::make_shared<TestEventTrackerFilterConfig>(proto_config);
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<TestEventTrackerFilter>(config));
  };
}

/**
 * Static registration for the TestEventTracker filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(TestEventTrackerFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace TestEventTracker
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
