#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/common/pure.h"
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
  resolve(const envoy::api::v2::core::SocketAddress& socket_address) PURE;

  const std::string category() const override { return "resolvers"; }
};

} // namespace Address
} // namespace Network
} // namespace Envoy
