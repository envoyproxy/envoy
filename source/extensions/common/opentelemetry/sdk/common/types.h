#pragma once

#include <map>
#include <string>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/proto/common/v1/common.pb.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace OpenTelemetry {
namespace Sdk {
namespace Common {

/**
 * Shared OpenTelemetry type aliases used across all telemetry signals.
 * These types are truly shared across trace, metrics, and logs signals.
 *
 * Origin: All types are derived from official OpenTelemetry C++ SDK and protocol definitions
 * Reference: https://github.com/open-telemetry/opentelemetry-cpp
 *
 * Note: Signal-specific types are located in their respective signal directories:
 * - Trace types: source/extensions/common/opentelemetry/sdk/trace/types.h
 * - Metrics types: source/extensions/common/opentelemetry/sdk/metrics/types.h
 * - Logs types: source/extensions/common/opentelemetry/sdk/logs/types.h
 */

/**
 * @brief Open-telemetry Attribute (from OpenTelemetry C++ SDK)
 * @see
 * https://github.com/open-telemetry/opentelemetry-cpp/blob/main/api/include/opentelemetry/common/attribute_value.h
 */
using OTelAttribute = ::opentelemetry::common::AttributeValue;

/**
 * @brief Container holding Open-telemetry Attributes (Envoy extension)
 */
using OTelAttributes = std::map<std::string, OTelAttribute>;

/**
 * @brief Common key-value pair type used in OpenTelemetry (from OTLP spec)
 */
using KeyValue = ::opentelemetry::proto::common::v1::KeyValue;

} // namespace Common
} // namespace Sdk
} // namespace OpenTelemetry
} // namespace Common
} // namespace Extensions
} // namespace Envoy
