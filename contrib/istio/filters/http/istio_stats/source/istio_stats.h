#pragma once

#include "envoy/server/filter_config.h"
#include "envoy/stream_info/filter_state.h"

#include "contrib/envoy/extensions/filters/http/istio_stats/v3/istio_stats.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IstioStats {

class IstioStatsFilterConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  std::string name() const override { return "envoy.filters.http.istio_stats"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<stats::PluginConfig>();
  }

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config, const std::string&,
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
