#pragma once

// NOLINT(namespace-envoy)

#include <optional>
#include <vector>

#include "absl/types/optional.h"
#include "headers.h"
#include "request_headers.h"

class RequestHeaders;

enum RetryRule {
  Status5xx,
  GatewayFailure,
  ConnectFailure,
  RefusedStream,
  Retriable4xx,
  RetriableHeaders,
  Reset,
};

struct RetryPolicy {
  int max_retry_count;
  std::vector<RetryRule> retry_on;
  std::vector<int> retry_status_codes;
  absl::optional<int> per_try_timeout_ms;
  absl::optional<int> total_upstream_timeout_ms;

  RawHeaders output_headers() const;
  static RetryPolicy from(const RequestHeaders& headers);
};

using RetryPolicySharedPtr = std::shared_ptr<RetryPolicy>;
