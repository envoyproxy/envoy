#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"
#include "envoy/http/header_map.h"

#include "extensions/common/matcher/matcher.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

using Matcher = Envoy::Extensions::Common::Matcher::Matcher;

using TraceWrapperPtr = std::unique_ptr<envoy::data::tap::v3::TraceWrapper>;
inline TraceWrapperPtr makeTraceWrapper() {
  return std::make_unique<envoy::data::tap::v3::TraceWrapper>();
}

/**
 * A handle for a per-tap sink. This allows submitting either a single buffered trace, or a series
 * of trace segments that the sink can aggregate in whatever way it chooses.
 */
class PerTapSinkHandle {
public:
  virtual ~PerTapSinkHandle() = default;

  /**
   * Send a trace wrapper to the sink. This may be a fully buffered trace or a segment of a larger
   * trace depending on the contents of the wrapper.
   * @param trace supplies the trace to send.
   * @param format supplies the output format to use.
   */
  virtual void submitTrace(TraceWrapperPtr&& trace,
                           envoy::config::tap::v3::OutputSink::Format format) PURE;
};

using PerTapSinkHandlePtr = std::unique_ptr<PerTapSinkHandle>;

/**
 * Wraps potentially multiple PerTapSinkHandle instances and any common pre-submit functionality.
 * Each active tap will have a reference to one of these, which in turn may have references to
 * one or more PerTapSinkHandle.
 */
class PerTapSinkHandleManager {
public:
  virtual ~PerTapSinkHandleManager() = default;

  /**
   * Submit a buffered or streamed trace segment to all managed per-tap sink handles.
   */
  virtual void submitTrace(TraceWrapperPtr&& trace) PURE;
};

using PerTapSinkHandleManagerPtr = std::unique_ptr<PerTapSinkHandleManager>;

/**
 * Sink for sending tap messages.
 */
class Sink {
public:
  virtual ~Sink() = default;

  /**
   * Create a per tap sink handle for use in submitting either buffered traces or trace segments.
   * @param trace_id supplies a locally unique trace ID. Some sinks use this for output generation.
   */
  virtual PerTapSinkHandlePtr createPerTapSinkHandle(uint64_t trace_id) PURE;
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
  virtual void newTapConfig(const envoy::config::tap::v3::TapConfig& proto_config,
                            Sink* admin_streamer) PURE;
};

/**
 * Abstract tap configuration base class.
 */
class TapConfig {
public:
  virtual ~TapConfig() = default;

  /**
   * Return a per-tap sink handle manager for use by a tap session.
   * @param trace_id supplies a locally unique trace ID. Some sinks use this for output generation.
   */
  virtual PerTapSinkHandleManagerPtr createPerTapSinkHandleManager(uint64_t trace_id) PURE;

  /**
   * Return the maximum received bytes that can be buffered in memory. Streaming taps are still
   * subject to this limit depending on match status.
   */
  virtual uint32_t maxBufferedRxBytes() const PURE;

  /**
   * Return the maximum transmitted bytes that can be buffered in memory. Streaming taps are still
   * subject to this limit depending on match status.
   */
  virtual uint32_t maxBufferedTxBytes() const PURE;

  /**
   * Return a new match status vector that is correctly sized for the number of matchers that are in
   * the configuration.
   */
  virtual Matcher::MatchStatusVector createMatchStatusVector() const PURE;

  /**
   * Return the root matcher for use in updating a match status vector.
   */
  virtual const Matcher& rootMatcher() const PURE;

  /**
   * Non-const version of rootMatcher method.
   */
  Matcher& rootMatcher() {
    return const_cast<Matcher&>(static_cast<const TapConfig&>(*this).rootMatcher());
  }

  /**
   * Return whether the tap session should run in streaming or buffering mode.
   */
  virtual bool streaming() const PURE;
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
  createConfigFromProto(const envoy::config::tap::v3::TapConfig& proto_config,
                        Sink* admin_streamer) PURE;
};

using TapConfigFactoryPtr = std::unique_ptr<TapConfigFactory>;

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
