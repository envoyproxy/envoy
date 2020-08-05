#pragma once

#include <cerrno>

#include "envoy/network/address.h"

namespace Envoy {
namespace Network {
namespace Address {

// TODO(junr03): https://github.com/envoyproxy/envoy/pull/9362/ introduced API surface to the
// codec's Stream interface that made it necessary for Stream to be aware of its underlying
// connection. This class is created in order to stub out Address for Stream implementations
// that have no backing connection, e.g Envoy Mobile's DirectStream. It might be possible to
// eliminate this dependency.
// TODO(junr03): consider moving this code to Envoy's codebase.
class SyntheticAddressImpl : public Instance {
public:
  SyntheticAddressImpl() {}

  bool operator==(const Instance&) const {
    // Every synthetic address is different from one another and other address types. In reality,
    // whatever object owns a synthetic address can't rely on address equality for any logic as the
    // address is just a stub.
    return false;
  }

  const std::string& asString() const { return address_; }

  absl::string_view asStringView() const { return address_; }

  const std::string& logicalName() const { return address_; }

  const Ip* ip() const { return nullptr; }

  const Pipe* pipe() const { return nullptr; }

  const sockaddr* sockAddr() const { return nullptr; }

  socklen_t sockAddrLen() const { return 0; }

  Type type() const {
    // TODO(junr03): consider adding another type of address.
    return Type::Ip;
  }

  const std::string& socketInterface() const { return address_; }

private:
  const std::string address_{"synthetic"};
};
} // namespace Address
} // namespace Network
} // namespace Envoy
