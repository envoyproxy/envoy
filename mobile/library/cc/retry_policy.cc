#include "retry_policy.h"

namespace Envoy {
namespace Platform {

RawHeaderMap RetryPolicy::asRawHeaderMap() const {
  RawHeaderMap outbound_headers{
      {"x-envoy-max-retries", {std::to_string(max_retry_count)}},
      {"x-envoy-upstream-rq-timeout-ms", {std::to_string(total_upstream_timeout_ms.value_or(0))}},
  };

  if (per_try_timeout_ms.has_value()) {
    outbound_headers["x-envoy-upstream-rq-per-try-timeout-ms"] =
        std::vector<std::string>{std::to_string(per_try_timeout_ms.value())};
  }

  std::vector<std::string> retry_on_copy;
  retry_on_copy.reserve(retry_on.size());
  for (const auto& retry_rule : retry_on) {
    retry_on_copy.push_back(retry_rule);
  }

  if (!retry_status_codes.empty()) {
    retry_on_copy.push_back("retriable-status-codes");
    std::vector<std::string> retry_status_codes_copy;
    retry_status_codes_copy.reserve(retry_status_codes.size());
    for (const auto& status_code : retry_status_codes) {
      retry_status_codes_copy.push_back(std::to_string(status_code));
    }
    outbound_headers["x-envoy-retriable-status-codes"] = retry_status_codes_copy;
  }

  if (!retry_on.empty()) {
    outbound_headers["x-envoy-retry-on"] = retry_on_copy;
  }

  return outbound_headers;
}

RetryPolicy RetryPolicy::fromRawHeaderMap(const RawHeaderMap& headers) {
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
      retry_policy.retry_on.push_back(retry_rule_str);
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
