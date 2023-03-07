#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "library/common/extensions/filters/http/test_accessor/filter.h"
#include "library/common/extensions/filters/http/test_accessor/filter.pb.h"
#include "library/common/extensions/filters/http/test_accessor/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestAccessor {

/**
 * Config registration for the TestAccessor filter. @see NamedHttpFilterConfigFactory.
 */
class TestAccessorFilterFactory
    : public Common::FactoryBase<
          envoymobile::extensions::filters::http::test_accessor::TestAccessor> {
public:
  TestAccessorFilterFactory() : FactoryBase("test_accessor") {}

private:
  ::Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::test_accessor::TestAccessor& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(TestAccessorFilterFactory);

} // namespace TestAccessor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
