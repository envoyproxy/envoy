#pragma once

#include "envoy/server/transport_socket_config.h"

#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/transport_sockets/internal_upstream/config.h"

#include "contrib/envoy/extensions/filters/network/istio_hbone/v3alpha/istio_hbone.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {

using FilterConfig = envoy::extensions::filters::network::istio_hbone::v3alpha::Config;
using SocketConfig = envoy::extensions::filters::network::istio_hbone::v3alpha::UpstreamConfig;

class FilterConfigFactory : public NetworkFilters::Common::ExceptionFreeFactoryBase<FilterConfig> {
public:
  FilterConfigFactory() : ExceptionFreeFactoryBase("envoy.filters.network.istio_hbone", true) {}

private:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const FilterConfig& config,
                                    Server::Configuration::FactoryContext& context) override;
};

class UpstreamSocketFactory : public Network::CommonUpstreamTransportSocketFactory {
public:
  UpstreamSocketFactory(const SocketConfig& config, Stats::Scope& scope);
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override;
  bool implementsSecureTransport() const override { return false; }
  absl::string_view defaultServerNameIndication() const override { return ""; }

private:
  const TransportSockets::InternalUpstream::Config metadata_config_;
};

class SocketConfigFactory : public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.istio_hbone"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<SocketConfig>();
  }
  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr> createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
