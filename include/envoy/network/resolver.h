#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/core/v3alpha/address.pb.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Interface for all network address resolvers.
 */
class Resolver {
public:
  virtual ~Resolver() = default;

  /**
   * Resolve a custom address string and port to an Address::Instance.
   * @param socket_address supplies the socket address to resolve.
   * @return InstanceConstSharedPtr appropriate Address::Instance.
   */
  virtual InstanceConstSharedPtr
  resolve(const envoy::config::core::v3alpha::SocketAddress& socket_address) PURE;

  /**
   * @return std::string the identifying name for a particular implementation of
   * a resolver.
   */
  virtual std::string name() const PURE;

  /**
   * @return std::string the identifying category name for objects
   * created by this factory. Used for automatic registration with
   * FactoryCategoryRegistry.
   */
  static std::string category() { return "resolvers"; }
};

} // namespace Address
} // namespace Network
} // namespace Envoy
