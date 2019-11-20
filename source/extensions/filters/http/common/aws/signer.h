#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/message.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Aws {

// TODO(lavignes): Move this interface to include/envoy if this is needed elsewhere
class Signer {
public:
  virtual ~Signer() = default;

  /**
   * Sign an AWS request.
   * @param message an AWS API request message.
   * @param sign_body include the message body in the signature. The body must be fully buffered.
   * @throws EnvoyException if the request cannot be signed.
   */
  virtual void sign(Http::Message& message, bool sign_body) PURE;
};

using SignerPtr = std::unique_ptr<Signer>;

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy