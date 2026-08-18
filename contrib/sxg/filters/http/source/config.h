#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.h"
#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

class FilterFactory : public Extensions::HttpFilters::Common::ExceptionFreeFactoryBase<
                          envoy::extensions::filters::http::sxg::v3alpha::SXG> {
public:
  FilterFactory() : ExceptionFreeFactoryBase("envoy.filters.http.sxg") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::sxg::v3alpha::SXG& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(FilterFactory);

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
