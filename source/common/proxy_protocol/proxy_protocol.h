#pragma once

#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/network/proxy_protocol.h"

#include "absl/types/span.h"

namespace Envoy {
namespace Common {
namespace ProxyProtocol {

// Parses proxy protocol TLVs from the given configuration.
Network::ProxyProtocolTLVVector
parseTLVs(absl::Span<const envoy::config::core::v3::ProxyProtocolTLV* const> tlvs);

} // namespace ProxyProtocol
} // namespace Common
} // namespace Envoy
