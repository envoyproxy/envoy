#pragma once

#include <functional>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Ssl {

enum class PrivateKeyOperationStatus {
  Success,
  Failure,
};

class PrivateKeyOperationsCallbacks {
public:
  virtual ~PrivateKeyOperationsCallbacks() {}

  /**
   * Callback function which is called when the asynchronous private key
   * operation has been completed.
   * @param status is "Success" or "Failure" depending on whether the private key operation was
   * successful or not.
   */
  virtual void complete(PrivateKeyOperationStatus status) PURE;
};

} // namespace Ssl
} // namespace Envoy
