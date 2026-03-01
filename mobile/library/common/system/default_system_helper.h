#pragma once

#include "library/common/system/system_helper.h"

namespace Envoy {

/**
 * Default implementation of SystemHelper which invokes the appropriate
 * platform-specific system APIs.
 */
class DefaultSystemHelper : public SystemHelper {
public:
  ~DefaultSystemHelper() override = default;

  // SystemHelper:
  bool isCleartextPermitted(absl::string_view hostname) override;
  envoy_cert_validation_result validateCertificateChain(const std::vector<std::string>& certs,
                                                        absl::string_view hostname) override;
  void cleanupAfterCertificateValidation() override;
  int64_t getDefaultNetworkHandle() override;
  std::vector<std::pair<int64_t, ConnectionType>> getAllConnectedNetworks() override;
  void bindSocketToNetwork(Network::ConnectionSocket& socket, int64_t network_handle) override;
};

} // namespace Envoy
