#pragma once

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/alpn/v3/alpn.pb.h"

namespace Envoy {
namespace Http {
namespace Alpn {

/**
 * Config registration for the alpn filter.
 */
class AlpnConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  // Server::Configuration::NamedHttpFilterConfigFactory
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;

private:
  Http::FilterFactoryCb createFilterFactory(
      const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig& config_pb,
      Upstream::ClusterManager& cluster_manager);
};

} // namespace Alpn
} // namespace Http
} // namespace Envoy
