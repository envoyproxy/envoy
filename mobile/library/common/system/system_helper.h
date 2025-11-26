#pragma once

#include <memory>
#include <vector>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "library/common/extensions/cert_validator/platform_bridge/c_types.h"
#include "library/common/network/network_types.h"

namespace Envoy {

namespace test {
class SystemHelperPeer;
} // namespace test

/**
 * SystemHelper provided a platform-agnostic API for interacting with platform-specific
 * system APIs. The singleton helper is accessible via the `getInstance()` static
 * method. Tests may override this singleton via `test::SystemHelperPeer` class.
 */
class SystemHelper {
public:
  virtual ~SystemHelper() = default;

  /**
   * @return true if the system permits cleartext requests.
   */
  virtual bool isCleartextPermitted(absl::string_view hostname) PURE;

  /**
   * Invokes platform APIs to validate certificates.
   */
  virtual envoy_cert_validation_result
  validateCertificateChain(const std::vector<std::string>& certs, absl::string_view hostname) PURE;

  /**
   * Invokes platform APIs to clean up after validation is complete.
   */
  virtual void cleanupAfterCertificateValidation() PURE;

  /**
   * Invokes platform APIs to retrieve a handle to the current default network.
   */
  virtual int64_t getDefaultNetworkHandle() PURE;

  virtual std::vector<std::pair<int64_t, ConnectionType>> getAllConnectedNetworks() PURE;
  /**
   * @return a reference to the current SystemHelper instance.
   */
  static SystemHelper& getInstance();

private:
  friend class test::SystemHelperPeer;

  static SystemHelper* instance_;
};

} // namespace Envoy
