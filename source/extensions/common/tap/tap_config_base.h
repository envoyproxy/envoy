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
  Extensions::Common::Tap::Sink& sink() { return *sink_to_use_; }

protected:
  TapConfigBaseImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                    Common::Tap::Sink* admin_streamer);

private:
  Sink* sink_to_use_;
  SinkPtr sink_;
  std::vector<MatcherPtr> matchers_;
};

/**
 * A tap sink that writes each tap trace to a discrete output file.
 */
class FilePerTapSink : public Sink {
public:
  FilePerTapSink(const envoy::service::tap::v2alpha::FilePerTapSink& config) : config_(config) {}

  // Sink
  void submitBufferedTrace(std::shared_ptr<envoy::data::tap::v2alpha::BufferedTraceWrapper> trace,
                           uint64_t trace_id) override;

private:
  const envoy::service::tap::v2alpha::FilePerTapSink config_;
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
