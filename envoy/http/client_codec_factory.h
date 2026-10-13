#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/random_generator.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Http {

/**
 * Per-cluster factory for the upstream (client) HTTP codec. An implementation is attached to a
 * cluster via typed_extension_protocol_options and surfaced through
 * ClusterInfo::upstreamHttpClientCodecFactory().
 */
class ClientCodecFactory {
public:
  virtual ~ClientCodecFactory() = default;

  /**
   * Everything CodecClientProd hands the stock codecs, so that a fresh codec can be constructed.
   */
  struct Context {
    // The negotiated codec type for this connection.
    CodecType type;
    // The underlying network connection the codec runs over.
    Network::Connection& connection;
    // The codec connection callbacks (the owning CodecClient).
    ConnectionCallbacks& callbacks;
    // The cluster the upstream host belongs to (source of stats, protocol options, header limits).
    const Upstream::ClusterInfo& cluster;
    // Random generator for codecs that need it (e.g. HTTP/2).
    Random::RandomGenerator& random;
    // The transport socket options for the connection, if any. Used e.g. by the HTTP/1 codec to
    // detect a proxied connection (http11ProxyInfo()).
    const std::shared_ptr<const Network::TransportSocketOptions>& options;
  };

  /**
   * @param context supplies the materials needed to build a fresh codec.
   * @return the client codec to use for this connection, or nullptr to indicate that the stock
   *         codec CodecClientProd would otherwise build should be used. An implementation that
   *         wants to decorate the stock behavior constructs its own codec from @p context (the
   *         stock codec cannot be wrapped after construction, because intercepting inbound events
   *         such as GOAWAY requires owning the ConnectionCallbacks at construction time).
   *
   *         An implementation that returns a non-null codec takes over the full construction the
   *         stock path would otherwise perform. In particular, for an HTTP/3 connection it must
   *         initialize the QUIC session itself (dynamic_cast context.connection to
   *         EnvoyQuicClientSession and call Initialize()), since CodecClientProd only does so on
   *         the stock path. Returning nullptr for unsupported codec types defers to the stock
   *         codec (e.g. the reverse-tunnel codec handles only HTTP/2 and returns nullptr
   *         otherwise).
   */
  virtual ClientConnectionPtr createClientCodec(const Context& context) const PURE;
};

} // namespace Http
} // namespace Envoy
