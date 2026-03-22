#include <android/multinetwork.h>
#include <errno.h>

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"

#include "library/common/system/default_system_helper.h"
#include "library/jni/android_jni_utility.h"
#include "library/jni/android_network_utility.h"

namespace Envoy {

bool DefaultSystemHelper::isCleartextPermitted(absl::string_view hostname) {
  return JNI::isCleartextPermitted(hostname);
}

envoy_cert_validation_result
DefaultSystemHelper::validateCertificateChain(const std::vector<std::string>& certs,
                                              absl::string_view hostname) {
  return JNI::verifyX509CertChain(certs, hostname);
}

void DefaultSystemHelper::cleanupAfterCertificateValidation() { JNI::jvmDetachThread(); }

int64_t DefaultSystemHelper::getDefaultNetworkHandle() { return JNI::getDefaultNetworkHandle(); }

std::vector<std::pair<int64_t, ConnectionType>> DefaultSystemHelper::getAllConnectedNetworks() {
  return JNI::getAllConnectedNetworks();
}

void DefaultSystemHelper::bindSocketToNetwork(Network::ConnectionSocket& socket,
                                              int64_t network_handle) {
  if (!socket.ioHandle().isOpen()) {
    ENVOY_LOG_MISC(warn, "Socket is not open, not binding to network");
    return;
  }
  int fd = socket.ioHandle().fdDoNotUse();
  int rc = android_setsocknetwork(static_cast<net_handle_t>(network_handle), fd);
  if (rc != 0) {
    ENVOY_LOG_MISC(warn, "Failed to bind socket to network {}: {}, closing socket", network_handle,
                   Envoy::errorDetails(errno));
    socket.close();
  }
}

} // namespace Envoy
