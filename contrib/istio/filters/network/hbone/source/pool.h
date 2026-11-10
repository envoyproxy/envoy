#pragma once

#include "envoy/router/router.h"
#include "envoy/tcp/upstream.h"

#include "contrib/envoy/extensions/filters/network/istio_hbone/v3alpha/istio_hbone.pb.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {

using PoolConfig = envoy::extensions::filters::network::istio_hbone::v3alpha::ConnectionPoolConfig;

class HttpPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string category() const override { return "envoy.upstreams"; }
  std::string name() const override { return "envoy.upstreams.istio_hbone"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<PoolConfig>();
  }
  Router::GenericConnPoolPtr
  createGenericConnPool(Upstream::HostConstSharedPtr host, Upstream::ThreadLocalCluster& cluster,
                        UpstreamProtocol protocol, Upstream::ResourcePriority priority,
                        std::optional<Http::Protocol> downstream_protocol,
                        Upstream::LoadBalancerContext* context,
                        const Protobuf::Message& config) const override;
};

class TcpPoolFactory : public TcpProxy::GenericConnPoolFactory {
public:
  std::string category() const override { return "envoy.upstreams"; }
  std::string name() const override { return "envoy.upstreams.istio_hbone"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<PoolConfig>();
  }
  TcpProxy::GenericConnPoolPtr
  createGenericConnPool(Upstream::HostConstSharedPtr host, Upstream::ThreadLocalCluster& cluster,
                        TcpProxy::TunnelingConfigHelperOptConstRef config,
                        Upstream::LoadBalancerContext* context,
                        Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
                        Http::StreamDecoderFilterCallbacks& decoder_callbacks,
                        StreamInfo::StreamInfo& downstream_info) const override;
};

} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
