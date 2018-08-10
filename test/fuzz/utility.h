#pragma once

#include "test/common/access_log/test_util.h"
#include "test/fuzz/common.pb.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Fuzz {

// Convert from test proto Headers to TestHeaderMapImpl.
inline Http::TestHeaderMapImpl fromHeaders(const test::fuzz::Headers& headers) {
  Http::TestHeaderMapImpl header_map;
  for (const auto& header : headers.headers()) {
    header_map.addCopy(header.key(), header.value());
  }
  return header_map;
}

inline TestRequestInfo fromRequestInfo(const test::fuzz::RequestInfo& request_info) {
  TestRequestInfo test_request_info;
  test_request_info.metadata_ = request_info.dynamic_metadata();
  test_request_info.start_time_ = SystemTime(std::chrono::microseconds(request_info.start_time()));
  if (request_info.has_response_code()) {
    test_request_info.response_code_ = request_info.response_code().value();
  }
  return test_request_info;
}

} // namespace Fuzz
} // namespace Envoy
