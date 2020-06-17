#pragma once

#include "envoy/tcp/factory.h"

namespace Envoy {
namespace Tcp {

class NamedTcpUpstreamFactory : public Config::TypedFactory {
  ~NamedTcpUpstreamFactory() override = default;
  virtual std::shared_ptr<TcpUpstreamFactory>
  createTcpUpstreamFactoryFromProto(const Protobuf::Message& config) PURE;
  std::string category() const override { return "envoy.upstream.tcp"; }
};
} // namespace Tcp
} // namespace Envoy