#pragma once

#include "envoy/service/tap/v2alpha/common.pb.h"

#include "extensions/common/tap/tap.h"
#include "extensions/common/tap/tap_matcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

/**
 * Base class for all tap configurations.
 * TODO(mattklein123): This class will handle common functionality such as rate limiting, etc.
 */
class TapConfigBaseImpl {
public:
  size_t numMatchers() { return matchers_.size(); }
  Matcher& rootMatcher();
  Extensions::Common::Tap::Sink& sink() {
    // TODO(mattklein123): When we support multiple sinks, select the right one. Right now
    // it must be admin.
    return *admin_streamer_;
  }

protected:
  TapConfigBaseImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                    Common::Tap::Sink* admin_streamer);

private:
  Sink* admin_streamer_;
  std::vector<MatcherPtr> matchers_;
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
