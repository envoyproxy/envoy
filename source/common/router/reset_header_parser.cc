#include "source/common/router/reset_header_parser.h"

#include <cstdint>

#include "source/common/common/assert.h"

#include "absl/strings/numbers.h"

namespace Envoy {
namespace Router {

ResetHeaderParserImpl::ResetHeaderParserImpl(
    const envoy::config::route::v3::RetryPolicy::ResetHeader& config)
    : name_(config.name()) {
  switch (config.format()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::route::v3::RetryPolicy::SECONDS:
    format_ = ResetHeaderFormat::Seconds;
    break;
  case envoy::config::route::v3::RetryPolicy::UNIX_TIMESTAMP:
    format_ = ResetHeaderFormat::UnixTimestamp;
    break;
  }
}

absl::optional<std::chrono::milliseconds>
ResetHeaderParserImpl::parseInterval(TimeSource& time_source,
                                     const Http::HeaderMap& headers) const {
  const auto header = headers.get(name_);

  if (header.empty()) {
    return absl::nullopt;
  }

  // This is effectively a trusted header so per the API only using the first value is used.
  const auto& header_value = header[0]->value().getStringView();
  uint64_t num_seconds{};

  switch (format_) {
  case ResetHeaderFormat::Seconds:
    if (absl::SimpleAtoi(header_value, &num_seconds)) {
      return absl::optional<std::chrono::milliseconds>(num_seconds * 1000UL);
    }
    break;

  case ResetHeaderFormat::UnixTimestamp:
    if (absl::SimpleAtoi(header_value, &num_seconds)) {
      const uint64_t timestamp = DateUtil::nowToSeconds(time_source);

      if (num_seconds < timestamp) {
        return absl::nullopt;
      }

      const uint64_t interval = num_seconds - timestamp;
      return absl::optional<std::chrono::milliseconds>(interval * 1000UL);
    }
    break;
  }

  return absl::nullopt;
}

} // namespace Router
} // namespace Envoy
