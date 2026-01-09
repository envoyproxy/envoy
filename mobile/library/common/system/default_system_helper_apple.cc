#include "source/common/common/assert.h"

#include "library/common/network/apple_platform_cert_verifier.h"
#include "library/common/system/default_system_helper.h"

namespace Envoy {

bool DefaultSystemHelper::isCleartextPermitted(absl::string_view /*hostname*/) { return true; }

envoy_cert_validation_result
DefaultSystemHelper::validateCertificateChain(const std::vector<std::string>& certs,
                                              absl::string_view hostname) {
  return verify_cert(certs, hostname);
}

void DefaultSystemHelper::cleanupAfterCertificateValidation() {}

int64_t DefaultSystemHelper::getDefaultNetworkHandle() { return -1; }

std::vector<std::pair<int64_t, ConnectionType>> DefaultSystemHelper::getAllConnectedNetworks() {
  return {};
}

void DefaultSystemHelper::bindSocketToNetwork(Network::ConnectionSocket&, int64_t) {
  // iOS network monitor doesn't propagate network handle to native code, so this should not be
  // called.
  PANIC("unreachable");
}

} // namespace Envoy
