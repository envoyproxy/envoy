#pragma once

#include "envoy/service/tap/v2alpha/common.pb.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

/**
 * Base class for all tap configurations.
 * TODO(mattklein123): This class will handle common functionality such as rate limiting, etc.
 */
class TapConfigBaseImpl {
protected:
  TapConfigBaseImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config)
      : proto_config_(std::move(proto_config)) {}

  const envoy::service::tap::v2alpha::TapConfig proto_config_;
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
