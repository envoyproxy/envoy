#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Interface for all network address resolvers.
 */
class Resolver : public Config::UntypedFactory {
public:
  virtual ~Resolver() = default;

  /**
   * Resolve a custom address string and port to an Address::Instance.
   * @param socket_address supplies the socket address to resolve.
   * @return InstanceConstSharedPtr appropriate Address::Instance.
   */
  virtual InstanceConstSharedPtr
  resolve(const envoy::config::core::v3::SocketAddress& socket_address) PURE;

  std::string category() const override { return "envoy.resolvers"; }
};

} // namespace Address
} // namespace Network
} // namespace Envoy
