#pragma once

#include "envoy/extensions/filters/http/peak_ewma/v3/peak_ewma.pb.h"
#include "envoy/extensions/filters/http/peak_ewma/v3/peak_ewma.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PeakEwma {

class PeakEwmaFilterConfigFactory
    : public Extensions::HttpFilters::Common::FactoryBase<
          envoy::extensions::filters::http::peak_ewma::v3::PeakEwmaConfig> {
public:
  PeakEwmaFilterConfigFactory() : FactoryBase("envoy.filters.http.peak_ewma") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::peak_ewma::v3::PeakEwmaConfig& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace PeakEwma
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
