#include "extensions/common/tap/tap_config_base.h"

#include <fstream>

#include "common/common/assert.h"

#include "extensions/common/tap/tap_matcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

TapConfigBaseImpl::TapConfigBaseImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                                     Common::Tap::Sink* admin_streamer) {
  ASSERT(proto_config.output_config().sinks().size() == 1);
  switch (proto_config.output_config().sinks()[0].output_sink_type_case()) {
  case envoy::service::tap::v2alpha::OutputSink::kStreamingAdmin:
    // TODO(mattklein123): Graceful failure, error message, and test if someone specifies an
    // admin stream output without configuring via /tap.
    RELEASE_ASSERT(admin_streamer != nullptr, "admin output must be configured via admin");
    sink_to_use_ = admin_streamer;
    break;
  case envoy::service::tap::v2alpha::OutputSink::kFilePerTap:
    sink_ =
        std::make_unique<FilePerTapSink>(proto_config.output_config().sinks()[0].file_per_tap());
    sink_to_use_ = sink_.get();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  buildMatcher(proto_config.match_config(), matchers_);
}

Matcher& TapConfigBaseImpl::rootMatcher() {
  ASSERT(matchers_.size() >= 1);
  return *matchers_[0];
}

void FilePerTapSink::submitBufferedTrace(
    std::shared_ptr<envoy::data::tap::v2alpha::BufferedTraceWrapper> trace, uint64_t trace_id) {
  // TODO(mattklein123): Add JSON format.
  const bool text_format =
      config_.format() == envoy::service::tap::v2alpha::FilePerTapSink::PROTO_TEXT;
  const std::string path =
      fmt::format("{}_{}.{}", config_.path_prefix(), trace_id, text_format ? "pb_text" : "pb");
  ENVOY_LOG_MISC(debug, "Writing tap for [id={}] to {}", trace_id, path);
  ENVOY_LOG_MISC(trace, "Tap for [id={}]: {}", trace_id, trace->DebugString());
  std::ofstream proto_stream(path);
  if (text_format) {
    proto_stream << trace->DebugString();
  } else {
    trace->SerializeToOstream(&proto_stream);
  }
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
