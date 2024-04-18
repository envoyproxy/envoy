#pragma once

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Types {

/**
 * A wrapper around envoy_headers that's responsible for freeing
 * the underlying headers when they are not needed anymore.
 */
class ManagedEnvoyHeaders {
public:
  /**
   * Initialize a new instance of the receiver using a given instance of envoy headers.
   *
   * @param headers, that should be wrapped by the receiver. The wrapper will hold onto
   *                 the passed headers and free them once the receiver is not used anymore.
   */
  ManagedEnvoyHeaders(envoy_headers headers) : headers_(headers){};
  ~ManagedEnvoyHeaders() { release_envoy_headers(headers_); }
  const envoy_headers& get() const { return headers_; }

private:
  envoy_headers headers_;
  // Make copy and assignment operators private to prevent copying of the receiver.
  ManagedEnvoyHeaders(const ManagedEnvoyHeaders&);
  ManagedEnvoyHeaders& operator=(const ManagedEnvoyHeaders&);
};

} // namespace Types
} // namespace Envoy
