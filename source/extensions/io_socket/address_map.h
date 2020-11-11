#pragma once

#include "envoy/network/address.h"

namespace Envoy {
namespace Network {
namespace Address {

class AddressMap {
public:
  constexpr absl::string_view operator[](Type address_type) const {
    switch (address_type) {
    case Type::Ip:
      return IpName;
    case Type::Pipe:
      return PipeName;
    case Type::EnvoyInternal:
      return EnvoyInternalName;
    }
  }
};

constexpr AddressMap addressMap;

} // namespace Address
} // namespace Network
} // namespace Envoy