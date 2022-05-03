#pragma once

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

class SslExtendedSocketInfo;

class ValidateResultCallback {
public:
  virtual ~ValidateResultCallback() = default;

  virtual Event::Dispatcher& dispatcher() PURE;

  virtual void onCertValidationResult(bool succeeded, const std::string& error_details) PURE;
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

  virtual SSL* ssl() PURE;

  /**
   * called only when doing asynchronous cert validation.
   * @return ValidateResultCallbackPtr a callback used to return the validation result.
   */
  virtual ValidateResultCallbackPtr createValidateResultCallback() PURE;

  /**
   * Called after the cert validation completes.
   */
  virtual void onCertificateValidationCompleted(bool succeeded) PURE;

  /**
   * @return ValidateResult either Successful or Failed after the validation
   * completed. Pending during the validation.
   */
  virtual ValidateResult certificateValidationResult() PURE;
};

} // namespace Ssl
} // namespace Envoy
