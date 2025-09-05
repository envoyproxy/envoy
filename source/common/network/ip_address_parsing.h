#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/platform.h"

#include "source/common/common/statusor.h"

namespace Envoy {
namespace Network {

// Utilities for parsing numeric IP addresses into sockaddr structures.
// These helper methods avoid higher-level dependencies and are suitable for
// use by multiple components that need low-level parsing without constructing
// `Address::Instance` objects.
namespace IpAddressParsing {

// Parse an IPv4 address string into a sockaddr_in with the provided port.
// Returns a failure status if the address is not a valid numeric IPv4 string.
StatusOr<sockaddr_in> parseIPv4(const std::string& ip_address, uint16_t port);

// Parse an IPv6 address string (optionally with a scope id, e.g. ``fe80::1%2``
// or ``fe80::1%eth0``) into a sockaddr_in6 with the provided port.
//
// Uses getaddrinfo() with ``AI_NUMERICHOST|AI_NUMERICSERV`` to avoid DNS lookups
// and to support scoped addresses consistently across all platforms.
//
// Returns a failure status if the address is not a valid numeric IPv6 string.
StatusOr<sockaddr_in6> parseIPv6(const std::string& ip_address, uint16_t port);

} // namespace IpAddressParsing
} // namespace Network
} // namespace Envoy
