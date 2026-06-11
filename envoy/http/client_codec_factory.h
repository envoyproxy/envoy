#pragma once

#include <functional>

#include "envoy/http/codec.h"

namespace Envoy {

namespace Network {
class Connection;
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
   * Builds the stock client codec that CodecClientProd would otherwise build for this connection.
   */
  using CreateDefaultCodecCb = std::function<ClientConnectionPtr()>;

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
  };

  /**
   * @param context supplies the materials needed to build a fresh codec.
   * @param create_default builds the stock codec, for implementations that prefer to decorate.
   * @return the codec to use for this connection. Returning create_default() is a valid no-op.
   */
  virtual ClientConnectionPtr
  createClientCodec(const Context& context, const CreateDefaultCodecCb& create_default) const PURE;
};

} // namespace Http
} // namespace Envoy
