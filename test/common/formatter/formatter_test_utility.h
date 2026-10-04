#pragma once

#include <optional>
#include <string>

#include "envoy/formatter/substitution_formatter.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/formatter/stream_info_formatter.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Formatter {

/**
 * Runs both the deprecated format() and the new formatTo() on the given provider, asserts that
 * the two agree, and returns format()'s result.
 *
 * Tests call this instead of provider.format() so that every existing expectation also covers
 * the sink-based path. The returned value is exactly what format() returned, so the caller's
 * assertion is unchanged.
 */
std::optional<std::string> formatForTest(const FormatterProvider& provider, const Context& context,
                                         const StreamInfo::StreamInfo& stream_info);

/**
 * Runs both the deprecated formatValue() and the new formatValueTo() on the given provider,
 * asserts that the two agree, and returns formatValue()'s result.
 *
 * The two paths are compared as serialized JSON: formatValueTo() only ever produces JSON text,
 * so comparing the text is both parser-free and exactly what a JSON formatter would emit. It
 * also checks that the sink is left untouched precisely when formatValue() reports no value,
 * which is the convention the JSON formatters rely on to omit a key or emit null.
 */
Protobuf::Value formatValueForTest(const FormatterProvider& provider, const Context& context,
                                   const StreamInfo::StreamInfo& stream_info);

/**
 * The same two helpers for StreamInfoFormatterProvider's context-free overloads, which are the
 * ones its subclasses actually implement.
 */
std::optional<std::string> formatForTest(const StreamInfoFormatterProvider& provider,
                                         const StreamInfo::StreamInfo& stream_info);
Protobuf::Value formatValueForTest(const StreamInfoFormatterProvider& provider,
                                   const StreamInfo::StreamInfo& stream_info);

} // namespace Formatter
} // namespace Envoy
