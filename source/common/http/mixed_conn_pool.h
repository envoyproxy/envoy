#pragma once

#include "source/common/http/conn_pool_base.h"

namespace Envoy {
namespace Http {

// An HTTP connection pool which supports both HTTP/1 and HTTP/2 based on ALPN
class HttpConnPoolImplMixed : public HttpConnPoolImplBase {
public:
  HttpConnPoolImplMixed(
      Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
      Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
      const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
      Upstream::ClusterConnectivityState& state)
      : HttpConnPoolImplBase(std::move(host), std::move(priority), dispatcher, options,
                             transport_socket_options, random_generator, state,
                             {Protocol::Http2, Protocol::Http11}) {}

  Envoy::ConnectionPool::ActiveClientPtr instantiateActiveClient() override;
  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override;

  void onConnected(Envoy::ConnectionPool::ActiveClient& client) override;
  Http::Protocol protocol() { return protocol_; }

  absl::string_view protocolDescription() const override { return "HTTP/1 HTTP/2 ALPN"; }

private:
  // Default to HTTP/1, as servers which don't support ALPN are probably HTTP/1 only.
  Http::Protocol protocol_ = Protocol::Http11;
};

} // namespace Http
} // namespace Envoy
