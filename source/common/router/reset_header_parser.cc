#include "common/router/reset_header_parser.h"

#include <cstdint>

#include "common/common/assert.h"

#include "absl/strings/numbers.h"

namespace Envoy {
namespace Router {

ResetHeaderParserImpl::ResetHeaderParserImpl(
    const envoy::config::route::v3::RetryPolicy::ResetHeader& config)
    : name_(config.name()) {
  switch (config.format()) {
  case envoy::config::route::v3::RetryPolicy::SECONDS:
    format_ = ResetHeaderFormat::Seconds;
    break;
  case envoy::config::route::v3::RetryPolicy::UNIX_TIMESTAMP:
    format_ = ResetHeaderFormat::UnixTimestamp;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

absl::optional<std::chrono::milliseconds>
ResetHeaderParserImpl::parseInterval(TimeSource& time_source,
                                     const Http::HeaderMap& headers) const {
  const Http::HeaderEntry* header = headers.get(name_);

  if (header == nullptr) {
    return absl::nullopt;
  }

  const auto& header_value = header->value().getStringView();
  uint64_t num_seconds{};

  switch (format_) {
  case ResetHeaderFormat::Seconds:
    if (absl::SimpleAtoi(header_value, &num_seconds)) {
      return absl::optional<std::chrono::milliseconds>(num_seconds * 1000UL);
    }
    break;

  case ResetHeaderFormat::UnixTimestamp:
    if (absl::SimpleAtoi(header_value, &num_seconds)) {
      const auto time_now = time_source.systemTime().time_since_epoch();
      const uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(time_now).count();

      if (num_seconds < timestamp) {
        return absl::nullopt;
      }

      const uint64_t interval = num_seconds - timestamp;
      return absl::optional<std::chrono::milliseconds>(interval * 1000UL);
    }
    break;

  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  return absl::nullopt;
}

} // namespace Router
} // namespace Envoy
