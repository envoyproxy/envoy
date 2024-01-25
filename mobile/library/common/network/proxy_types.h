#pragma once

#include <memory>

#include "library/common/network/proxy_resolver_interface.h"

namespace Envoy {
namespace Network {

struct ProxyResolverApi {
  std::unique_ptr<ProxyResolver> resolver;
};

} // namespace Network
} // namespace Envoy
