#include "test/common/http/filters/test_event_tracker/config.h"

#include "test/common/http/filters/test_event_tracker/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestEventTracker {

absl::StatusOr<Http::FilterFactoryCb>
TestEventTrackerFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::test_event_tracker::TestEventTracker&
        proto_config,
    Server::Configuration::ServerFactoryContext&, Server::Configuration::ExtraFactoryContext&) {

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
