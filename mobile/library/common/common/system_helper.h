#pragma once

#include <memory>

#include "absl/strings/string_view.h"
#include "envoy/common/pure.h"
#include "library/common/extensions/cert_validator/platform_bridge/c_types.h"

namespace Envoy {

/**
 * This class is used instead of Envoy::MainCommon to customize logic for the Envoy Mobile setting.
 * It largely leverages Envoy::StrippedMainBase.
 */
class SystemHelper {
public:
  /**
   * @return a reference to the current SystemHelper instance.
   */
  static SystemHelper& getInstance();

  virtual ~SystemHelper() = default;

  /**
   * @return true if the system permits cleartext requests.
   */
  virtual bool isCleartextPermitted(absl::string_view hostname) PURE;

  /**
   * Invokes platform APIs to validate certificates.
   */
  virtual envoy_cert_validation_result validateCertificateChain(const envoy_data* certs, uint8_t size,
                                                        const char* host_name) PURE;
  /**
   * Invokes platform APIs to clean up after validation is complete.
   */
  virtual void cleanupAfterCertificateValidation() PURE;

private:
  static std::unique_ptr<SystemHelper> instance_;
};

} // namespace Envoy
