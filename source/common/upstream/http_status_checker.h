#pragma once

#include <vector>

#include "envoy/api/v2/core/health_check.pb.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

class HttpStatusChecker;
typedef std::unique_ptr<HttpStatusChecker> HttpStatusCheckerPtr;

/**
 * Utility class checking if given http status matches configured expectations.
 */
class HttpStatusChecker {
public:
  static HttpStatusCheckerPtr
  configure(const Protobuf::RepeatedPtrField<envoy::type::Int64Range>& expected_statuses,
            uint64_t default_expected_status);

  bool isExpected(uint64_t http_status) const;

protected:
  HttpStatusChecker() {}

private:
  std::vector<std::pair<uint64_t, uint64_t>> ranges_;
};

} // namespace Upstream
} // namespace Envoy
