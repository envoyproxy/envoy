#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/service/tap/v2alpha/common.pb.h"

#include "extensions/common/tap/tap.h"
#include "extensions/common/tap/tap_matcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

/**
 * Common utilities for tapping.
 */
class Utility {
public:
  /**
   * Add body data to a tapped body message, taking into account the maximum bytes to buffer.
   */
  static void addBufferToProtoBytes(envoy::data::tap::v2alpha::Body& output_body,
                                    uint32_t max_buffered_bytes, const Buffer::Instance& data);
};

/**
 * Base class for all tap configurations.
 * TODO(mattklein123): This class will handle common functionality such as rate limiting, etc.
 */
class TapConfigBaseImpl {
public:
  uint32_t maxBufferedRxBytes() const { return max_buffered_rx_bytes_; }
  uint32_t maxBufferedTxBytes() const { return max_buffered_tx_bytes_; }
  size_t numMatchers() const { return matchers_.size(); }
  Matcher& rootMatcher() const;
  void
  submitBufferedTrace(const std::shared_ptr<envoy::data::tap::v2alpha::BufferedTraceWrapper>& trace,
                      uint64_t trace_id);

protected:
  TapConfigBaseImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                    Common::Tap::Sink* admin_streamer);

private:
  // This is the default setting for both RX/TX max buffered bytes. (This means that per tap, the
  // maximum amount that can be buffered is 2x this value).
  static constexpr uint32_t DefaultMaxBufferedBytes = 1024;

  const uint32_t max_buffered_rx_bytes_;
  const uint32_t max_buffered_tx_bytes_;
  Sink* sink_to_use_;
  SinkPtr sink_;
  envoy::service::tap::v2alpha::OutputSink::Format sink_format_;
  std::vector<MatcherPtr> matchers_;
};

/**
 * A tap sink that writes each tap trace to a discrete output file.
 */
class FilePerTapSink : public Sink {
public:
  FilePerTapSink(const envoy::service::tap::v2alpha::FilePerTapSink& config) : config_(config) {}

  // Sink
  void
  submitBufferedTrace(const std::shared_ptr<envoy::data::tap::v2alpha::BufferedTraceWrapper>& trace,
                      envoy::service::tap::v2alpha::OutputSink::Format format,
                      uint64_t trace_id) override;

private:
  const envoy::service::tap::v2alpha::FilePerTapSink config_;
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
