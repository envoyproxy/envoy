#pragma once

#include "envoy/extensions/filters/http/original_src/v3/original_src.pb.h"
#include "envoy/extensions/filters/http/original_src/v3/original_src.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {
/**
 * Config registration for the original_src filter.
 */
class OriginalSrcConfigFactory
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::original_src::v3::OriginalSrc> {
public:
  OriginalSrcConfigFactory() : UnifiedFactoryBase("envoy.filters.http.original_src") {}

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::original_src::v3::OriginalSrc& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
