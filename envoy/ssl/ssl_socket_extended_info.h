#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"

#include "connection.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

enum class ClientValidationStatus { NotValidated, NoClientCertificate, Validated, Failed };
enum class ValidateResult {
  Successful,
  Failed,
  Pending,
};

class ValidateResultCallback {
public:
  virtual ~ValidateResultCallback() = default;

  virtual Event::Dispatcher& dispatcher() PURE;

  virtual void onCertValidationResult(bool succeeded, const std::string& error_details,
                                      uint8_t out_alert) PURE;
};

using ValidateResultCallbackPtr = std::unique_ptr<ValidateResultCallback>;

class SslExtendedSocketInfo {
public:
  virtual ~SslExtendedSocketInfo() = default;

  /**
   * Set the peer certificate validation status.
   **/
  virtual void setCertificateValidationStatus(ClientValidationStatus validated) PURE;

  /**
   * @return ClientValidationStatus The peer certificate validation status.
   **/
  virtual ClientValidationStatus certificateValidationStatus() const PURE;

  /**
   * Only called when doing asynchronous cert validation.
   * @return ValidateResultCallbackPtr a callback used to return the validation result.
   */
  virtual ValidateResultCallbackPtr createValidateResultCallback(uint8_t* current_tls_alert) PURE;

  /**
   * Called after the cert validation completes.
   */
  virtual void onCertificateValidationCompleted(bool succeeded) PURE;

  /**
   * @return absl::optional<ValidateResult> returns nullopt if no validation is going on. Returns
   * either Successful or Failed after the validation completed, and pending during the validation.
   */
  virtual absl::optional<ValidateResult> certificateValidationResult() PURE;

  /**
   * Called when doing asynchronous cert validation.
   * @return uint8_t represents the tls alert populated by cert validator.
   */
  virtual uint8_t tlsAlert() const PURE;
};

} // namespace Ssl
} // namespace Envoy
