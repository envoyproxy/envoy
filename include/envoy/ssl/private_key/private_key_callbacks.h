#pragma once

#include <functional>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Ssl {

class PrivateKeyConnectionCallbacks {
public:
  virtual ~PrivateKeyConnectionCallbacks() = default;

  /**
   * Callback function which is called when the asynchronous private key
   * operation has been completed (with either success or failure). The
   * provider will communicate the success status when SSL_do_handshake()
   * is called the next time.
   */
  virtual void onPrivateKeyMethodComplete() PURE;
};

} // namespace Ssl
} // namespace Envoy
