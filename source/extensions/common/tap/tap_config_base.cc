#include "extensions/common/tap/tap_config_base.h"

#include "common/common/assert.h"

#include "extensions/common/tap/tap_matcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

TapConfigBaseImpl::TapConfigBaseImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                                     Common::Tap::Sink* admin_streamer)
    : admin_streamer_(admin_streamer) {

  // TODO(mattklein123): The streaming admin output sink is the only currently supported sink. This
  // is validated by schema.
  ASSERT(admin_streamer != nullptr);
  ASSERT(proto_config.output_config().sinks()[0].has_streaming_admin());

  buildMatcher(proto_config.match_config(), matchers_);
}

Matcher& TapConfigBaseImpl::rootMatcher() {
  ASSERT(matchers_.size() >= 1);
  return *matchers_[0];
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
