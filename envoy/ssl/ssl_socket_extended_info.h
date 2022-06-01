#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Ssl {

enum class ClientValidationStatus { NotValidated, NoClientCertificate, Validated, Failed };

enum class ValidateStatus {
  NotStarted,
  Pending,
  Successful,
  Failed,
};

class ValidateResultCallback {
public:
  virtual ~ValidateResultCallback() = default;

  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Called when the asynchronous cert validation completes.
   * @param succeeded true if the validation succeeds
   * @param error_details failure details, only used if the validation fails.
   * @param out_alert the SSL error related to the failure, only used if the validation fails.
   */
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
  virtual ValidateResultCallbackPtr createValidateResultCallback(uint8_t current_tls_alert) PURE;

  /**
   * Called after the cert validation completes either synchronously or asynchronously.
   * @param succeeded true if the validation succeeded.
   */
  virtual void onCertificateValidationCompleted(bool succeeded) PURE;

  /**
   * @return ValidateStatus the validation status.
   */
  virtual ValidateStatus certificateValidationResult() const PURE;

  /**
   * Called when doing asynchronous cert validation.
   * @return uint8_t represents the tls alert populated by cert validator.
   */
  virtual uint8_t tlsAlert() const PURE;
};

} // namespace Ssl
} // namespace Envoy
