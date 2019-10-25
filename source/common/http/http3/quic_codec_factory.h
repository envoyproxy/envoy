#pragma once

#include <string>

#include "envoy/http/codec.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Http {

class QuicHttpConnectionFactory {
public:
  virtual ~QuicHttpConnectionFactory() {}

  virtual std::string name() const PURE;

  virtual Connection* createQuicHttpConnection(Network::Connection& connection,
                                               ConnectionCallbacks& callbacks) PURE;
};

} // namespace Http
} // namespace Envoy
