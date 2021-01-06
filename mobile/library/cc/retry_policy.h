#pragma once

#include <optional>
#include <vector>

#include "absl/types/optional.h"
#include "headers.h"
#include "request_headers.h"

namespace Envoy {
namespace Platform {

class RequestHeaders;

enum RetryRule {
  Status5xx,
  GatewayError,
  ConnectFailure,
  RefusedStream,
  Retriable4xx,
  RetriableHeaders,
  Reset,
};

std::string retry_rule_to_string(RetryRule retry_rule);
RetryRule retry_rule_from_string(const std::string& str);

struct RetryPolicy {
  int max_retry_count;
  std::vector<RetryRule> retry_on;
  std::vector<int> retry_status_codes;
  absl::optional<int> per_try_timeout_ms;
  absl::optional<int> total_upstream_timeout_ms;

  RawHeaderMap as_raw_header_map() const;
  static RetryPolicy from_raw_header_map(const RawHeaderMap& headers);
};

using RetryPolicySharedPtr = std::shared_ptr<RetryPolicy>;

} // namespace Platform
} // namespace Envoy
