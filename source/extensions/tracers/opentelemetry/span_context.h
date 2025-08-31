#pragma once

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * This class represents the context of an OpenTelemetry span, including the following
 * characteristics: trace id, span id, parent id, and trace flags.
 */
class SpanContext {
public:
  /*
   * Default constructor creates an empty context.
   */
  SpanContext() = default;

  /*
   * Constructor that creates a context object from the supplied attributes.
   */
  SpanContext(absl::string_view version, absl::string_view trace_id, absl::string_view span_id,
              bool sampled, std::string tracestate)
      : version_(version), trace_id_(trace_id), span_id_(span_id), sampled_(sampled),
        tracestate_(std::move(tracestate)) {}

  /**
   * @return the span's version as a hex string.
   */
  const std::string& version() const { return version_; }

  /**
   * @return the span's id as a hex string.
   */
  const std::string& spanId() const { return span_id_; }

  /**
   * @return the trace id as an integer.
   */
  const std::string& traceId() const { return trace_id_; }

  /**
   * @return the sampled flag.
   */
  bool sampled() const { return sampled_; }

  /**
   * @return the parsed tracestate header.
   */
  const std::string& tracestate() const { return tracestate_; }

private:
  std::string version_;
  std::string trace_id_;
  std::string span_id_;
  bool sampled_{false};
  std::string tracestate_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
