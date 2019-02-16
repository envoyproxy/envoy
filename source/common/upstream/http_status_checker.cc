#include "common/upstream/http_status_checker.h"

#include <vector>

#include "envoy/api/v2/core/health_check.pb.h"

#include "common/access_log/access_log_formatter.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

HttpStatusCheckerPtr HttpStatusChecker::configure(
    const Protobuf::RepeatedPtrField<envoy::type::Int64Range>& expected_statuses,
    uint64_t default_expected_status) {
  HttpStatusCheckerPtr httpStatusChecker(new HttpStatusChecker());

  for (const auto& status_range : expected_statuses) {
    auto start = status_range.start();
    auto end = status_range.end();

    if (start >= end) {
      throw EnvoyException(fmt::format(
          "Invalid http status range: expecting start < end, but found start={} and end={}", start,
          end));
    }

    if (start < 100) {
      throw EnvoyException(fmt::format(
          "Invalid http status range: expecting start >= 100, but found start={}", start));
    }

    if (end > 600) {
      throw EnvoyException(
          fmt::format("Invalid http status range: expecting end <= 600, but found end={}", end));
    }

    httpStatusChecker->ranges_.emplace_back(
        std::make_pair(static_cast<uint64_t>(start), static_cast<uint64_t>(end)));
  }

  if (httpStatusChecker->ranges_.empty()) {
    httpStatusChecker->ranges_.emplace_back(
        std::make_pair(default_expected_status, default_expected_status + 1));
  }

  return httpStatusChecker;
}

bool HttpStatusChecker::inRange(uint64_t http_status) const {
  for (const auto& range : ranges_) {
    if (http_status >= range.first && http_status < range.second) {
      return true;
    }
  }

  return false;
}

} // namespace Upstream
} // namespace Envoy
