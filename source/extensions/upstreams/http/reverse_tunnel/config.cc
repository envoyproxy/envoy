#include "source/extensions/upstreams/http/reverse_tunnel/config.h"

#include <chrono>
#include <memory>
#include <string>

#include "envoy/http/codes.h"
#include "envoy/server/admin.h"
#include "envoy/server/options.h"
#include "envoy/singleton/manager.h"

#include "source/common/common/logger.h"
#include "source/common/http/http2/codec_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/upstreams/http/reverse_tunnel/drain_aware_client_connection.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace ReverseTunnel {

namespace ReverseTunnelProto = envoy::extensions::upstreams::http::reverse_tunnel::v3;

SINGLETON_MANAGER_REGISTRATION(reverse_tunnel_upstream_codec_drain);

// The cluster type this drain-aware upstream codec applies to.
constexpr absl::string_view ReverseConnectionClusterType = "envoy.clusters.reverse_connection";

Envoy::Http::ClientConnectionPtr
ReverseTunnelUpstreamCodecOptions::createClientCodec(const Context& context) const {
  // Only HTTP/2 reverse-connection clusters get the drain-aware codec; otherwise return nullptr so
  // CodecClientProd uses the stock codec. The cluster-type gate lives here to keep the core
  // codec-factory seam generic.
  const auto custom_type = context.cluster.clusterType();
  const bool is_reverse_connection =
      custom_type.has_value() && custom_type->name() == ReverseConnectionClusterType;
  if (!enable_drain_with_goaway_ || context.type != Envoy::Http::CodecType::HTTP2 ||
      !is_reverse_connection) {
    return nullptr;
  }

  // Mirror the stock HTTP/2 client codec construction, but inject the drain-aware callbacks wrapper
  // so a received GOAWAY is observed.
  const Upstream::ClusterInfo& cluster = context.cluster;
  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(context.callbacks, stats_);
  auto inner = std::make_unique<DrainAwareHttp2ClientConnection>(
      context.connection, *callbacks, cluster.http2CodecStats(), context.random,
      cluster.httpProtocolOptions().http2Options(),
      cluster.maxResponseHeadersKb().value_or(Envoy::Http::DEFAULT_MAX_REQUEST_HEADERS_KB),
      cluster.maxResponseHeadersCount(), Envoy::Http::Http2::ProdNghttp2SessionFactory::get());
  auto* h2_codec = inner.get();
  return std::make_unique<DrainAwareClientConnection>(std::move(inner), std::move(callbacks),
                                                      stats_, context.connection.dispatcher(),
                                                      registry_, cluster.name(), h2_codec);
}

absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr>
ReverseTunnelUpstreamCodecFactory::createProtocolOptionsConfig(
    const Protobuf::Message& config,
    Server::Configuration::ProtocolOptionsFactoryContext& context) {
  const auto& typed_config = MessageUtil::downcastAndValidate<
      const ReverseTunnelProto::ReverseTunnelUpstreamCodecOptions&>(
      config, context.messageValidationVisitor());

  // Logged once per cluster config load, unlike the per-connection codec lifecycle logs.
  ENVOY_LOG_MISC(info,
                 "reverse_tunnel: drain-aware upstream codec configured (drain_with_goaway={})",
                 typed_config.enable_drain_with_goaway());

  auto& server_context = context.serverFactoryContext();

  // The drain registry and its admin trigger only matter when the drain-aware codec is enabled;
  // when disabled the codec is never installed, so skip both and leave the registry null.
  UpstreamCodecDrainRegistrySharedPtr registry;
  if (typed_config.enable_drain_with_goaway()) {
    registry = server_context.singletonManager().getTyped<UpstreamCodecDrainRegistry>(
        SINGLETON_MANAGER_REGISTERED_NAME(reverse_tunnel_upstream_codec_drain), [&server_context] {
          return std::make_shared<UpstreamCodecDrainRegistry>(server_context.threadLocal());
        });

    // Register the admin trigger once (subsequent clusters get addHandler() == false, which is
    // fine). The captured registry shared_ptr keeps it alive for the handler's lifetime.
    if (auto admin = server_context.admin(); admin.has_value()) {
      // Default the rotation grace window to the server's configured drain time (--drain-time-s) so
      // in-flight requests get the same window to finish as a normal drain; overridable per call
      // via the drain_time_ms query param.
      const uint64_t default_drain_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 server_context.options().drainTime())
                                                 .count();
      admin->addHandler(
          "/reverse_tunnel/drain_clusters",
          "gracefully drain reverse-tunnel upstream client codecs; optional query params: "
          "cluster=<name> (default: all), drain_time_ms=<n> (default: server drain time)",
          [registry,
           default_drain_time_ms](Envoy::Http::ResponseHeaderMap&, Buffer::Instance& response,
                                  Server::AdminStream& admin_stream) -> Envoy::Http::Code {
            const auto params = admin_stream.queryParams();
            const std::string cluster = params.getFirstValue("cluster").value_or("");
            uint64_t drain_time_ms = default_drain_time_ms;
            if (auto v = params.getFirstValue("drain_time_ms"); v.has_value()) {
              if (!absl::SimpleAtoi(v.value(), &drain_time_ms)) {
                response.add("reverse_tunnel: invalid drain_time_ms query param\n");
                return Envoy::Http::Code::BadRequest;
              }
            }
            registry->drainCluster(cluster, std::chrono::milliseconds(drain_time_ms));
            response.add(absl::StrCat("reverse_tunnel: draining upstream codecs (cluster='",
                                      cluster.empty() ? "<all>" : cluster,
                                      "', drain_time_ms=", drain_time_ms, ")\n"));
            return Envoy::Http::Code::OK;
          },
          /*removable=*/false, /*mutates_server_state=*/true);
    }
  }

  auto stats = ReverseTunnelUpstreamCodecStats::generate(server_context.scope());
  return std::make_shared<ReverseTunnelUpstreamCodecOptions>(typed_config, stats,
                                                             std::move(registry));
}

ProtobufTypes::MessagePtr ReverseTunnelUpstreamCodecFactory::createEmptyProtocolOptionsProto() {
  return std::make_unique<ReverseTunnelProto::ReverseTunnelUpstreamCodecOptions>();
}

ProtobufTypes::MessagePtr ReverseTunnelUpstreamCodecFactory::createEmptyConfigProto() {
  return std::make_unique<ReverseTunnelProto::ReverseTunnelUpstreamCodecOptions>();
}

REGISTER_FACTORY(ReverseTunnelUpstreamCodecFactory, Server::Configuration::ProtocolOptionsFactory);

} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
