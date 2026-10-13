#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "test/common/http/filters/assertion/filter.pb.h"
#include "test/common/http/filters/assertion/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Assertion {

/**
 * Config registration for the assertion filter. @see NamedHttpFilterConfigFactory.
 */
class AssertionFilterFactory : public Common::UnifiedFactoryBase<
                                   envoymobile::extensions::filters::http::assertion::Assertion> {
public:
  AssertionFilterFactory() : UnifiedFactoryBase("assertion") {}

private:
  absl::StatusOr<::Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::assertion::Assertion& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(AssertionFilterFactory);

} // namespace Assertion
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
