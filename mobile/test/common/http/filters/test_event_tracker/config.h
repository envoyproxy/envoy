#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "test/common/http/filters/test_event_tracker/filter.h"
#include "test/common/http/filters/test_event_tracker/filter.pb.h"
#include "test/common/http/filters/test_event_tracker/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestEventTracker {

/**
 * Config registration for the TestEventTracker filter. @see NamedHttpFilterConfigFactory.
 */
class TestEventTrackerFilterFactory
    : public Common::FactoryBase<
          envoymobile::extensions::filters::http::test_event_tracker::TestEventTracker> {
public:
  TestEventTrackerFilterFactory() : FactoryBase("test_event_tracker") {}

private:
  ::Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::test_event_tracker::TestEventTracker& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(TestEventTrackerFilterFactory);

} // namespace TestEventTracker
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
