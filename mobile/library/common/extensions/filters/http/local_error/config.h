#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "library/common/extensions/filters/http/local_error/filter.pb.h"
#include "library/common/extensions/filters/http/local_error/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalError {

/**
 * Config registration for the local_error filter. @see NamedHttpFilterConfigFactory.
 */
class LocalErrorFilterFactory
    : public Common::UnifiedFactoryBase<
          envoymobile::extensions::filters::http::local_error::LocalError> {
public:
  LocalErrorFilterFactory() : UnifiedFactoryBase("local_error") {}

private:
  absl::StatusOr<::Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::local_error::LocalError& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(LocalErrorFilterFactory);

} // namespace LocalError
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
