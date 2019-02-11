#pragma once

#include "envoy/common/pure.h"
#include "envoy/data/tap/v2alpha/wrapper.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/service/tap/v2alpha/common.pb.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

/**
 * Sink for sending tap messages.
 */
class Sink {
public:
  virtual ~Sink() = default;

  /**
   * Send a fully buffered trace to the sink.
   * @param trace supplies the trace to send. The trace message is a discrete trace message (as
   *        opposed to a portion of a larger trace that should be aggregated).
   * @param format supplies the output format to use.
   * @param trace_id supplies a locally unique trace ID. Some sinks use this for output generation.
   */
  virtual void
  submitBufferedTrace(const std::shared_ptr<envoy::data::tap::v2alpha::BufferedTraceWrapper>& trace,
                      envoy::service::tap::v2alpha::OutputSink::Format format,
                      uint64_t trace_id) PURE;
};

using SinkPtr = std::unique_ptr<Sink>;

/**
 * Generic configuration for a tap extension (filter, transport socket, etc.).
 */
class ExtensionConfig {
public:
  virtual ~ExtensionConfig() = default;

  /**
   * @return the ID to use for admin extension configuration tracking (if applicable).
   */
  virtual const absl::string_view adminId() PURE;

  /**
   * Clear any active tap configuration.
   */
  virtual void clearTapConfig() PURE;

  /**
   * Install a new tap configuration.
   * @param proto_config supplies the generic tap config to install. Not all configuration fields
   *        may be applicable to an extension (e.g. HTTP fields). The extension is free to fail
   *        the configuration load via exception if it wishes.
   * @param admin_streamer supplies the singleton admin sink to use for output if the configuration
   *        specifies that output type. May not be used if the configuration does not specify
   *        admin output. May be nullptr if admin is not used to supply the config.
   */
  virtual void newTapConfig(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                            Sink* admin_streamer) PURE;
};

/**
 * Abstract tap configuration base class. Used for type safety.
 */
class TapConfig {
public:
  virtual ~TapConfig() = default;
};

using TapConfigSharedPtr = std::shared_ptr<TapConfig>;

/**
 * Abstract tap configuration factory. Given a new generic tap configuration, produces an
 * extension specific tap configuration.
 */
class TapConfigFactory {
public:
  virtual ~TapConfigFactory() = default;

  /**
   * @return a new configuration given a raw tap service config proto. See
   * ExtensionConfig::newTapConfig() for param info.
   */
  virtual TapConfigSharedPtr
  createConfigFromProto(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                        Sink* admin_streamer) PURE;
};

using TapConfigFactoryPtr = std::unique_ptr<TapConfigFactory>;

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
