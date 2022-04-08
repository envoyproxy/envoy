#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace Router {

/**
 * The router which holds all clusters and determines the cluster which UDP data should be routed
 * to.
 */
class Router {
public:
  virtual ~Router() = default;

  /**
   * Based on the address of the incoming UDP data, determine the target route for the data.
   * @return the cluster name or empty string if there is not matching route for the data.
   */
  virtual const std::string route(const Network::Address::Instance& destination_address,
                                  const Network::Address::Instance& source_address) const PURE;

  /**
   * Returns all cluster names in the router. The UDP proxy filter requires every cluster name for
   * initialization which will call this method on construction.
   * @return vector of all cluster names.
   */
  virtual const std::vector<std::string>& allClusterNames() const PURE;
};

using RouterConstSharedPtr = std::shared_ptr<const Router>;

} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
