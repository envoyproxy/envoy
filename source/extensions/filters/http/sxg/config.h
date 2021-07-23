#pragma once

#include <string>

#include "envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.h"
#include "envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

class FilterFactory : public Extensions::HttpFilters::Common::FactoryBase<
                          envoy::extensions::filters::http::sxg::v3alpha::SXG> {
public:
  FilterFactory() : FactoryBase("envoy.filters.http.sxg") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::sxg::v3alpha::SXG& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(FilterFactory);

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
