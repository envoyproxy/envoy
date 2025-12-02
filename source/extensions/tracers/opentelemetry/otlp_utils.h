#pragma once

#include <map>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/trace/v1/trace.pb.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief The type of the span.
 * see
 * https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/trace/api.md#spankind
 */
using OTelSpanKind = ::opentelemetry::proto::trace::v1::Span::SpanKind;

/**
 * @brief Based on Open-telemetry OwnedAttributeValue
 * see
 * https://github.com/open-telemetry/opentelemetry-cpp/blob/main/sdk/include/opentelemetry/sdk/common/attribute_utils.h
 */
using OTelAttribute =
    absl::variant<bool, int32_t, uint32_t, int64_t, double, std::string, absl::string_view,
                  std::vector<bool>, std::vector<int32_t>, std::vector<uint32_t>,
                  std::vector<int64_t>, std::vector<double>, std::vector<std::string>,
                  std::vector<absl::string_view>, uint64_t, std::vector<uint64_t>,
                  std::vector<uint8_t>>;

/**
 * @brief Container holding Open-telemetry Attributes
 */
using OtelAttributes = std::map<std::string, OTelAttribute>;

/**
 * Contains utility functions  for Otel
 */
class OtlpUtils {

public:
  /**
   * @brief Get the User-Agent header value to be used on the OTLP exporter request.
   *
   * The header value is compliant with the OpenTelemetry specification. See:
   * https://github.com/open-telemetry/opentelemetry-specification/blob/v1.30.0/specification/protocol/exporter.md#user-agent
   * @return std::string The User-Agent for the OTLP exporters in Envoy.
   */
  static const std::string& getOtlpUserAgentHeader();

  /**
   * @brief Set the Otel attribute on a Proto Value object
   *
   * @param value_proto Proto object which gets the value set.
   * @param attribute_value Value to set on the proto object.
   */
  static void populateAnyValue(opentelemetry::proto::common::v1::AnyValue& value_proto,
                               const OTelAttribute& attribute_value);
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
