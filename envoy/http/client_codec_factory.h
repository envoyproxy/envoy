#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/http/codec.h"

namespace Envoy {

namespace Network {
class Connection;
class TransportSocketOptions;
} // namespace Network

namespace Random {
class RandomGenerator;
} // namespace Random

namespace Upstream {
class ClusterInfo;
} // namespace Upstream

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
   */
  virtual ClientConnectionPtr createClientCodec(const Context& context) const PURE;
};

} // namespace Http
} // namespace Envoy
