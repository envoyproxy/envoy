#pragma once

#include <cstdint>


#include "envoy/extensions/upstreams/http/reverse_conn/v3/reverse_conn_pool.pb.h"

#include "envoy/event/dispatcher.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/upstream.h"

#include "source/common/http/http2/conn_pool.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

/**
 * Implementation of an active client for Reverse connections
 */
class ActiveClient : public Http::Http2::ActiveClient {
public:
  ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
               OptRef<Upstream::Host::CreateConnectionData> data,
               Http::CreateConnectionDataFn connection_fn = nullptr);
};

class ReverseConnPoolFactoryImpl : public Http::Http2::ReverseConnPoolFactory {
public:
  Http::ConnectionPool::InstancePtr allocateConnPool(
      Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
      Singleton::Manager& singleton_manager, Upstream::HostConstSharedPtr host,
      Upstream::ResourcePriority priority,
      const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
      Upstream::ClusterConnectivityState& state,
      absl::optional<Http::HttpServerPropertiesCache::Origin> origin = absl::nullopt,
      Http::HttpServerPropertiesCacheSharedPtr http_server_properties_cache = nullptr) override;

  std::string name() const override { return "envoy.http.reverse_conn.default"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::upstreams::http::reverse_conn::v3::ReverseConnPoolProto>();
  }
};

DECLARE_FACTORY(ReverseConnPoolFactoryImpl);

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy
