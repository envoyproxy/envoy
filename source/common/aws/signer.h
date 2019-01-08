#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/message.h"

namespace Envoy {
namespace Aws {
namespace Auth {

class Signer {
public:
  virtual ~Signer() = default;

  /**
   * Sign an AWS request.
   * @param message an
   */
  virtual void sign(Http::Message& message) const PURE;
};

typedef std::shared_ptr<Signer> SignerSharedPtr;

} // namespace Auth
} // namespace Aws
} // namespace Envoy