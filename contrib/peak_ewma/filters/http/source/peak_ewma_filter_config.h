#pragma once

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/peak_ewma/v3alpha/peak_ewma.pb.h"
#include "contrib/envoy/extensions/filters/http/peak_ewma/v3alpha/peak_ewma.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PeakEwma {

class PeakEwmaFilterConfigFactory
    : public Extensions::HttpFilters::Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::peak_ewma::v3alpha::PeakEwmaConfig> {
public:
  PeakEwmaFilterConfigFactory() : UnifiedFactoryBase("envoy.filters.http.peak_ewma") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::peak_ewma::v3alpha::PeakEwmaConfig& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

} // namespace PeakEwma
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
