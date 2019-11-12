#pragma once

#include <string>

#include "envoy/http/codec.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Http {

// A factory to create Http::Connection instance for QUIC. It has two registered
// subclasses QuicHttpClientConnectionFactory and QuicHttpServerConnectionFactory.
class QuicHttpConnectionFactory {
public:
  virtual ~QuicHttpConnectionFactory() {}

  virtual std::string name() const PURE;

  virtual Connection* createQuicHttpConnection(Network::Connection& connection,
                                               ConnectionCallbacks& callbacks) PURE;

  static std::string category() { return "quic_codec"; }
};

} // namespace Http
} // namespace Envoy
