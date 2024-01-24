#pragma once

#include "library/common/network/proxy_resolver_interface.h"
#include "library/common/network/proxy_types.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Network {

struct ProxyResolverApi {
  std::unique_ptr<ProxyResolver> resolver;
};

} // namespace Network
} // namespace Envoy
