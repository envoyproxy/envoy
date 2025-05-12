#include "source/extensions/tracers/datadog/dict_util.h"

#include <cstddef>

#include "envoy/http/header_map.h"
#include "envoy/tracing/trace_context.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

RequestHeaderWriter::RequestHeaderWriter(Http::RequestHeaderMap& headers) : headers_(headers) {}

void RequestHeaderWriter::set(datadog::tracing::StringView key,
                              datadog::tracing::StringView value) {
  headers_.setCopy(Http::LowerCaseString{key}, value);
}

ResponseHeaderReader::ResponseHeaderReader(const Http::ResponseHeaderMap& headers)
    : headers_(headers) {}

datadog::tracing::Optional<datadog::tracing::StringView>
ResponseHeaderReader::lookup(datadog::tracing::StringView key) const {
  auto result = headers_.get(Http::LowerCaseString{key});
  if (result.empty()) {
    // `headers_.get` can return multiple header entries. It conveys
    // "not found" by returning zero header entries.
    return datadog::tracing::nullopt;
  }

  if (result.size() == 1) {
    return result[0]->value().getStringView();
  }

  // There's more than one matching header entry.
  // Per RFC 2616, this is the same as if there were only one entry whose
  // value is the comma-separated concatenation of the multiple values.
  // I don't expect the Agent to repeat response headers, and we don't even
  // examine the Agent's response headers, but here's a solution anyway.
  std::vector<absl::string_view> values;
  values.reserve(result.size());
  for (std::size_t i = 0; i < result.size(); ++i) {
    values.push_back(result[i]->value().getStringView());
  }
  buffer_ = absl::StrJoin(values, ", ");
  return buffer_;
}

void ResponseHeaderReader::visit(
    const std::function<void(datadog::tracing::StringView key, datadog::tracing::StringView value)>&
        visitor) const {
  headers_.iterate([&](const Http::HeaderEntry& entry) {
    visitor(entry.key().getStringView(), entry.value().getStringView());
    return Http::ResponseHeaderMap::Iterate::Continue;
  });
}

TraceContextReader::TraceContextReader(const Tracing::TraceContext& context) : context_(context) {}

datadog::tracing::Optional<datadog::tracing::StringView>
TraceContextReader::lookup(datadog::tracing::StringView key) const {
  return context_.get(key);
}

void TraceContextReader::visit(
    const std::function<void(datadog::tracing::StringView key, datadog::tracing::StringView value)>&
        visitor) const {
  context_.forEach([&](absl::string_view key, absl::string_view value) {
    visitor(key, value);
    const bool continue_iterating = true;
    return continue_iterating;
  });
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
