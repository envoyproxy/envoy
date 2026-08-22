#pragma once

#include "envoy/server/filter_config.h"
#include "envoy/stream_info/filter_state.h"

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/istio_stats/v3/istio_stats.pb.h"
#include "contrib/envoy/extensions/filters/http/istio_stats/v3/istio_stats.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IstioStats {

class IstioStatsFilterConfigFactory : public Common::ExceptionFreeFactoryBase<stats::PluginConfig> {
public:
  IstioStatsFilterConfigFactory() : ExceptionFreeFactoryBase("envoy.filters.http.istio_stats") {}

private:
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const stats::PluginConfig& proto_config, const std::string&,
                                    Server::Configuration::FactoryContext&) override;
};

class IstioStatsNetworkFilterConfigFactory
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  std::string name() const override { return "envoy.filters.network.istio_stats"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<stats::PluginConfig>();
  }

  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& factory_context) override;
};

} // namespace IstioStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
