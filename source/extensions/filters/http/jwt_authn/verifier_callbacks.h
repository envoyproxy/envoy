#pragma once

#include "envoy/common/pure.h"

#include "jwt_verify_lib/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class VerifyContext;

/**
 * Handle for notifying Verifier callers of request completion.
 */
class VerifierCallbacks {
public:
  virtual ~VerifierCallbacks() {}

  /**
   * Called on completion of request.
   *
   * @param status the status of the request.
   */
  virtual void onComplete(const ::google::jwt_verify::Status& status) PURE;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
