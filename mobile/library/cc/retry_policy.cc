#include "retry_policy.h"

namespace Envoy {
namespace Platform {

static const std::pair<RetryRule, std::string> RETRY_RULE_LOOKUP[]{
    {RetryRule::Status5xx, "5xx"},
    {RetryRule::GatewayError, "gateway-error"},
    {RetryRule::ConnectFailure, "connect-failure"},
    {RetryRule::RefusedStream, "refused-stream"},
    {RetryRule::Retriable4xx, "retriable-4xx"},
    {RetryRule::RetriableHeaders, "retriable-headers"},
    {RetryRule::Reset, "reset"},
};

std::string retry_rule_to_string(RetryRule retry_rule) {
  for (const auto& pair : RETRY_RULE_LOOKUP) {
    if (pair.first == retry_rule) {
      return pair.second;
    }
  }
  throw std::invalid_argument("invalid retry rule");
}

RetryRule retry_rule_from_string(const std::string& str) {
  for (const auto& pair : RETRY_RULE_LOOKUP) {
    if (pair.second == str) {
      return pair.first;
    }
  }
  throw std::invalid_argument("invalid retry rule");
}

RawHeaderMap RetryPolicy::as_raw_header_map() const {
  RawHeaderMap outbound_headers{
      {"x-envoy-max-retries", {std::to_string(this->max_retry_count)}},
      {"x-envoy-upstream-rq-timeout-ms",
       {std::to_string(this->total_upstream_timeout_ms.value_or(0))}},
  };

  if (this->per_try_timeout_ms.has_value()) {
    outbound_headers["x-envoy-upstream-rq-per-try-timeout-ms"] =
        std::vector<std::string>{std::to_string(this->per_try_timeout_ms.value())};
  }

  std::vector<std::string> retry_on;
  for (const auto& retry_rule : this->retry_on) {
    retry_on.push_back(retry_rule_to_string(retry_rule));
  }

  if (this->retry_status_codes.size() > 0) {
    retry_on.push_back("retriable-status-codes");
    std::vector<std::string> retry_status_codes;
    for (const auto& status_code : this->retry_status_codes) {
      retry_status_codes.push_back(std::to_string(status_code));
    }
    outbound_headers["x-envoy-retriable-status-codes"] = retry_status_codes;
  }

  if (retry_on.size() > 0) {
    outbound_headers["x-envoy-retry-on"] = retry_on;
  }

  return outbound_headers;
}

RetryPolicy RetryPolicy::from_raw_header_map(const RawHeaderMap& headers) {
  RetryPolicy retry_policy;

  if (headers.contains("x-envoy-max-retries")) {
    retry_policy.max_retry_count = std::stoi(headers.at("x-envoy-max-retries")[0]);
  }

  if (headers.contains("x-envoy-upstream-rq-timeout-ms")) {
    retry_policy.total_upstream_timeout_ms =
        std::stoi(headers.at("x-envoy-upstream-rq-timeout-ms")[0]);
  }

  if (headers.contains("x-envoy-upstream-rq-per-try-timeout-ms")) {
    retry_policy.per_try_timeout_ms =
        std::stoi(headers.at("x-envoy-upstream-rq-per-try-timeout-ms")[0]);
  }

  bool has_retriable_status_codes = false;
  if (headers.contains("x-envoy-retry-on")) {
    for (const auto& retry_rule_str : headers.at("x-envoy-retry-on")) {
      if (retry_rule_str == "retriable-status_codes") {
        has_retriable_status_codes = true;
        continue;
      }
      retry_policy.retry_on.push_back(retry_rule_from_string(retry_rule_str));
    }
  }

  if (has_retriable_status_codes && headers.contains("x-envoy-retriable-status-codes")) {
    for (const auto& status_code_str : headers.at("x-envoy-retriable-status-codes")) {
      retry_policy.retry_status_codes.push_back(std::stoi(status_code_str));
    }
  }

  return retry_policy;
}

} // namespace Platform
} // namespace Envoy
