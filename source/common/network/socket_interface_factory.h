#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/socket.h"
#include "envoy/registry/registry.h"

#include "common/singleton/threadsafe_singleton.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Network {

class SocketInterfaceFactory : public virtual Config::TypedFactory {
public:
  virtual SocketInterfacePtr createSocketInterface(const Protobuf::Message& config) PURE;
  std::string category() const override { return "envoy.config.core.socket_interface"; }
};

class DefaultSocketInterfaceFactory : public SocketInterfaceFactory {
public:
  SocketInterfacePtr createSocketInterface(const Protobuf::Message& config) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return "envoy.config.core.default_socket_interface"; }
};

DECLARE_FACTORY(DefaultSocketInterfaceFactory);

using SocketInterfacesMap = absl::flat_hash_map<std::string, SocketInterfacePtr>;
using SocketInterfacesSingleton = InjectableSingleton<SocketInterfacesMap>;

static inline const SocketInterface* socketInterface(std::string name) {
  auto it = SocketInterfacesSingleton::get().find(name);
  if (it == SocketInterfacesSingleton::get().end()) {
    return nullptr;
  }
  return it->second.get();
}

} // namespace Network
} // namespace Envoy