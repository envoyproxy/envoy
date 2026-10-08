#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.h"
#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

class FilterFactory : public Extensions::HttpFilters::Common::UnifiedFactoryBase<
                          envoy::extensions::filters::http::sxg::v3alpha::SXG> {
public:
  FilterFactory() : UnifiedFactoryBase("envoy.filters.http.sxg") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::sxg::v3alpha::SXG& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(FilterFactory);

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
