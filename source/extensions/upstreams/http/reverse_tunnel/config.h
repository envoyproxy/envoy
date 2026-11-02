#pragma once

#include "envoy/extensions/upstreams/http/reverse_tunnel/v3/reverse_tunnel_codec.pb.h"
#include "envoy/extensions/upstreams/http/reverse_tunnel/v3/reverse_tunnel_codec.pb.validate.h"
#include "envoy/http/client_codec_factory.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/upstream/upstream.h"

#include "source/extensions/upstreams/http/reverse_tunnel/drain_registry.h"
#include "source/extensions/upstreams/http/reverse_tunnel/reverse_tunnel_codec_stats.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace ReverseTunnel {

/**
 * Per-cluster upstream codec options for reverse-tunnel clusters. The object doubles as the
 * Http::ClientCodecFactory (surfaced via ProtocolOptionsConfig::upstreamHttpClientCodecFactory()),
 * so the upstream HTTP/2 client codec can be made drain-aware.
 */
class ReverseTunnelUpstreamCodecOptions : public Upstream::ProtocolOptionsConfig,
                                          public Envoy::Http::ClientCodecFactory {
public:
  ReverseTunnelUpstreamCodecOptions(const envoy::extensions::upstreams::http::reverse_tunnel::v3::
                                        ReverseTunnelUpstreamCodecOptions& proto,
                                    const ReverseTunnelUpstreamCodecStats& stats,
                                    UpstreamCodecDrainRegistrySharedPtr registry)
      : enable_drain_with_goaway_(proto.enable_drain_with_goaway()), stats_(stats),
        registry_(std::move(registry)) {}

  // Upstream::ProtocolOptionsConfig
  OptRef<const Envoy::Http::ClientCodecFactory> upstreamHttpClientCodecFactory() const override {
    return *this;
  }

  // Envoy::Http::ClientCodecFactory
  Envoy::Http::ClientConnectionPtr createClientCodec(const Context& context) const override;

private:
  const bool enable_drain_with_goaway_;
  const ReverseTunnelUpstreamCodecStats stats_;
  const UpstreamCodecDrainRegistrySharedPtr registry_;
};

/**
 * Registers ReverseTunnelUpstreamCodecOptions as an upstream protocol options extension so it can
 * be attached to a cluster via typed_extension_protocol_options.
 */
class ReverseTunnelUpstreamCodecFactory : public Server::Configuration::ProtocolOptionsFactory {
public:
  absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr> createProtocolOptionsConfig(
      const Protobuf::Message& config,
      Server::Configuration::ProtocolOptionsFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string category() const override { return "envoy.upstream_options"; }
  std::string name() const override {
    return "envoy.extensions.upstreams.http.reverse_tunnel.v3.ReverseTunnelUpstreamCodecOptions";
  }
};

DECLARE_FACTORY(ReverseTunnelUpstreamCodecFactory);

} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
