#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <sys/socket.h>

#include <algorithm>

#include "envoy/network/address.h"

#include "test/test_common/environment.h"

namespace epoll_server {

namespace {

int addressFamilyUnderTestHelper() {
  std::vector<Envoy::Network::Address::IpVersion> versions =
      Envoy::TestEnvironment::getIpVersionsForTest();
  if (std::find(versions.begin(), versions.end(), Envoy::Network::Address::IpVersion::v4) !=
      versions.end()) {
    return AF_INET;
  }
  if (std::find(versions.begin(), versions.end(), Envoy::Network::Address::IpVersion::v6) !=
      versions.end()) {
    return AF_INET6;
  }
  return -1;
}

} // namespace

// Returns the address family to be used for test. Return v4 if the environment
// supports v4 only or both v4 and v6. Otherwise return v6 or an invalid value.
int AddressFamilyUnderTestImpl() {
  static const int version = addressFamilyUnderTestHelper();
  return version;
}

} // namespace epoll_server
