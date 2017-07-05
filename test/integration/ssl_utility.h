#pragma once

#include "envoy/network/address.h"
#include "envoy/ssl/context_manager.h"

namespace Envoy {
namespace Ssl {

ClientContextPtr createClientSslContext(bool alpn, bool san, ContextManager& context_manager);

Network::Address::InstanceConstSharedPtr getSslAddress(const Network::Address::IpVersion& version,
                                                       int port);

} // namespace Ssl
} // namespace Envoy
