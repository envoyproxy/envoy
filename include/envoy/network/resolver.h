#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Interface for all network address resolvers
 */
class Resolver {
public:
  virtual ~Resolver() {}

  /**
   * Resolve a custom address string and port to an Address::Instance
   * @param address the address to resolve
   * @param supplies the port on the address
   * @return an appropriate Address::Instance
   */
  virtual Network::Address::InstanceConstSharedPtr resolve(const std::string& address,
                                                           const uint32_t port) PURE;

  /**
   * Resolve a custom address string and named port to an Address::Instance
   * @param address the address to resolve
   * @param named_port the named port to resolve
   * @return an appropriate Address::Instance
   */
  virtual Network::Address::InstanceConstSharedPtr resolve(const std::string& address,
                                                           const std::string& named_port) PURE;
};

typedef std::unique_ptr<Resolver> ResolverPtr;

/**
 * A factory for individual network address resolvers
 */
class ResolverFactory {
public:
  virtual ~ResolverFactory() {}

  virtual ResolverPtr create() const PURE;

  /**
   * @return std::string the identifying name for a particular implementation of
   * a resolver produced by the factory.
   */
  virtual std::string name() PURE;
};

typedef std::shared_ptr<ResolverFactory> ResolverFactorySharedPtr;

} // namespace Address
} // namespace Network
} // namespace Envoy
