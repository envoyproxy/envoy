#pragma once

#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "contrib/envoy/extensions/upstreams/http/tcp/golang/v3alpha/golang.pb.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {
namespace Golang {

/**
 * Config registration for the TcpConnPool. @see Router::GenericConnPoolFactory
 */
class GolangGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override { return "envoy.upstreams.http.tcp.golang"; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr createGenericConnPool(
      Upstream::HostConstSharedPtr host, Upstream::ThreadLocalCluster& thread_local_cluster,
      Router::GenericConnPoolFactory::UpstreamProtocol upstream_protocol,
      Upstream::ResourcePriority priority,
      absl::optional<Envoy::Http::Protocol> downstream_protocol, Upstream::LoadBalancerContext* ctx,
      const Protobuf::Message& config) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::tcp::golang::v3alpha::Config>();
  }
};

DECLARE_FACTORY(GolangGenericConnPoolFactory);

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
