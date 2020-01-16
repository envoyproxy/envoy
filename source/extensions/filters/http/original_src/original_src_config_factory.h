#pragma once

#include "envoy/extensions/filters/http/original_src/v3/original_src.pb.h"
#include "envoy/extensions/filters/http/original_src/v3/original_src.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {
/**
 * Config registration for the original_src filter.
 */
class OriginalSrcConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::original_src::v3::OriginalSrc> {
public:
  OriginalSrcConfigFactory() : FactoryBase(HttpFilterNames::get().OriginalSrc) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::original_src::v3::OriginalSrc& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
