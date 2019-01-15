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
   */
  virtual void sign(Http::Message& message) PURE;
};

typedef std::unique_ptr<Signer> SignerPtr;

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy