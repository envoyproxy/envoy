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
 * The router.
 */
class Router {
public:
  virtual ~Router() = default;

  virtual const std::string& route(Network::Address::InstanceConstSharedPtr address) const PURE;
  virtual const std::vector<std::string>& entries() const PURE;
};

using RouterConstSharedPtr = std::shared_ptr<const Router>;

} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
