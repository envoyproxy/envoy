#pragma once

#include <functional>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Ssl {

enum class PrivateKeyMethodStatus {
  Success,
  Failure,
};

class PrivateKeyConnectionCallbacks {
public:
  virtual ~PrivateKeyConnectionCallbacks() {}

  /**
   * Callback function which is called when the asynchronous private key
   * operation has been completed.
   * @param status is "Success" or "Failure" depending on whether the private key operation was
   * successful or not.
   */
  virtual void complete(PrivateKeyMethodStatus status) PURE;
};

} // namespace Ssl
} // namespace Envoy
