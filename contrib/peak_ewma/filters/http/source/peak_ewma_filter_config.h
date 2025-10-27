#pragma once

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/peak_ewma/v3alpha/peak_ewma.pb.h"
#include "contrib/envoy/extensions/filters/http/peak_ewma/v3alpha/peak_ewma.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PeakEwma {

class PeakEwmaFilterConfigFactory
    : public Extensions::HttpFilters::Common::FactoryBase<
          envoy::extensions::filters::http::peak_ewma::v3alpha::PeakEwmaConfig> {
public:
  PeakEwmaFilterConfigFactory() : FactoryBase("envoy.filters.http.peak_ewma") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::peak_ewma::v3alpha::PeakEwmaConfig& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace PeakEwma
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
