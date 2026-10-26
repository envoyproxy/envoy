#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "test/common/http/filters/test_accessor/filter.h"
#include "test/common/http/filters/test_accessor/filter.pb.h"
#include "test/common/http/filters/test_accessor/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestAccessor {

/**
 * Config registration for the TestAccessor filter. @see NamedHttpFilterConfigFactory.
 */
class TestAccessorFilterFactory
    : public Common::UnifiedFactoryBase<
          envoymobile::extensions::filters::http::test_accessor::TestAccessor> {
public:
  TestAccessorFilterFactory() : UnifiedFactoryBase("test_accessor") {}

private:
  absl::StatusOr<::Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::test_accessor::TestAccessor& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(TestAccessorFilterFactory);

} // namespace TestAccessor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
