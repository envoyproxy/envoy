#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"

namespace Envoy {
namespace Ssl {

// Opaque type defined and used by the ``ServerContext``.
struct TlsContext;

enum class ClientValidationStatus { NotValidated, NoClientCertificate, Validated, Failed };

enum class ValidateStatus {
  NotStarted,
  Pending,
  Successful,
  Failed,
};

enum class CertificateSelectionStatus {
  NotStarted,
  Pending,
  Successful,
  Failed,
};

/**
 * Used to return the result from an asynchronous cert validation.
 */
class ValidateResultCallback {
public:
  virtual ~ValidateResultCallback() = default;

  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Called when the asynchronous cert validation completes.
   * @param succeeded true if the validation succeeds
   * @param detailed_status detailed status of the underlying validation. Depending on the
   *        validation configuration, `succeeded` may be true but `detailed_status` might
   *        indicate a failure. This detailed status can be used to inform routing
   *        decisions.
   * @param error_details failure details, only used if the validation fails.
   * @param tls_alert the TLS error related to the failure, only used if the validation fails.
   */
  virtual void onCertValidationResult(bool succeeded, ClientValidationStatus detailed_status,
                                      const std::string& error_details, uint8_t tls_alert) PURE;
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
   * @return ValidateResultCallbackPtr a callback used to return the validation result.
   */
  virtual ValidateResultCallbackPtr createValidateResultCallback() PURE;

  /**
   * Called after the cert validation completes either synchronously or asynchronously.
   * @param succeeded true if the validation succeeded.
   * @param async true if the validation is completed asynchronously.
   */
  virtual void onCertificateValidationCompleted(bool succeeded, bool async) PURE;

  /**
   * @return ValidateStatus the validation status.
   */
  virtual ValidateStatus certificateValidationResult() const PURE;

  /**
   * Called when doing asynchronous cert validation.
   * @return uint8_t represents the TLS alert populated by cert validator in
   * case of failure.
   */
  virtual uint8_t certificateValidationAlert() const PURE;

  /**
   * @return CertificateSelectionCallbackPtr a callback used to return the cert selection result.
   */
  virtual CertificateSelectionCallbackPtr createCertificateSelectionCallback() PURE;

  /**
   * Called after the cert selection completes either synchronously or asynchronously.
   * @param selected_ctx selected Ssl::TlsContext, it's empty when selection failed.
   * @param async true if the validation is completed asynchronously.
   * @param staple true when need to set OCSP response.
   */
  virtual void onCertificateSelectionCompleted(OptRef<const Ssl::TlsContext> selected_ctx,
                                               bool staple, bool async) PURE;

  /**
   * @return CertificateSelectionStatus the cert selection status.
   */
  virtual CertificateSelectionStatus certificateSelectionResult() const PURE;
};

} // namespace Ssl
} // namespace Envoy
