#pragma once

#include "common/network/utility.h"

#include "test/common/access_log/test_util.h"
#include "test/fuzz/common.pb.h"
#include "test/mocks/upstream/host.h"
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

// Convert from HeaderMap to test proto Headers.
inline test::fuzz::Headers toHeaders(const Http::HeaderMapImpl headers) {
  test::fuzz::Headers fuzz_headers;
  headers.iterate(
      [](const Http::HeaderEntry& header, void* ctxt) -> Http::HeaderMap::Iterate {
        auto* fuzz_header = static_cast<test::fuzz::Headers*>(ctxt)->add_headers();
        fuzz_header->set_key(header.key().c_str());
        fuzz_header->set_value(header.value().c_str());
        return Http::HeaderMap::Iterate::Continue;
      },
      &fuzz_headers);
  return fuzz_headers;
}

inline TestRequestInfo fromRequestInfo(const test::fuzz::RequestInfo& request_info) {
  TestRequestInfo test_request_info;
  test_request_info.metadata_ = request_info.dynamic_metadata();
  test_request_info.start_time_ = SystemTime(std::chrono::microseconds(request_info.start_time()));
  if (request_info.has_response_code()) {
    test_request_info.response_code_ = request_info.response_code().value();
  }
  auto upstream_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  auto upstream_metadata =
      std::make_shared<envoy::api::v2::core::Metadata>(request_info.upstream_metadata());
  ON_CALL(*upstream_host, metadata()).WillByDefault(testing::Return(upstream_metadata));
  test_request_info.upstream_host_ = upstream_host;
  auto address = Network::Utility::resolveUrl("tcp://10.0.0.1:443");
  test_request_info.upstream_local_address_ = address;
  test_request_info.downstream_local_address_ = address;
  test_request_info.downstream_remote_address_ = address;
  return test_request_info;
}

} // namespace Fuzz
} // namespace Envoy
